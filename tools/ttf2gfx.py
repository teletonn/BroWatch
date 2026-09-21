"""TrueType -> Adafruit GFX font header, the format TFT_eSPI's free fonts use.

    python tools/ttf2gfx.py <font.ttf> <pixel size> <CName> <out.h> [--static] [--wght N] [--wdth N] [--thresh N]

--static writes file-local arrays (include the header from exactly one .cpp,
which is how include/fonts/ is used: theme.cpp owns the bubble face). The
emulator builds the same header, PROGMEM being empty there. Glyphs 32..126, 1-bit, rendered by FreeType
through Pillow and thresholded.

--codes A,B-C,... restricts/extends the codepoint set, e.g.
    python tools/ttf2gfx.py font.ttf 8 RuCyr out.h --codes 1025,1040-1103,1105
The header then covers first..last contiguously (gaps become empty glyphs).

--supersample N renders at size*N and downscales: denser small glyphs.
--thresh N keeps pixels >= N (default 110); higher = thinner.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

args = sys.argv[1:]
path, size, cname, out = args[0], int(args[1]), args[2], args[3]
sim = '--sim' in args
static = '--static' in args or sim
thresh = int(args[args.index('--thresh') + 1]) if '--thresh' in args else 110
font = ImageFont.truetype(path, size)
axes = {}
if '--wght' in args: axes['wght'] = float(args[args.index('--wght') + 1])
if '--wdth' in args: axes['wdth'] = float(args[args.index('--wdth') + 1])
if axes:
    try:
        names = [a['name'].decode() if isinstance(a['name'], bytes) else a['name'] for a in font.get_variation_axes()]
        tags = [n.lower() for n in names]
        cur = [a['default'] for a in font.get_variation_axes()]
        for k, v in axes.items():
            for i, n in enumerate(tags):
                if n.startswith(k[:3]) or (k == 'wght' and 'weight' in n) or (k == 'wdth' and 'width' in n):
                    cur[i] = v
        font.set_variation_by_axes(cur)
    except Exception as e:
        print('variation not applied:', e)

X0, Y0 = 32, 64
bitmaps = bytearray()
glyphs = []
# --supersample N renders at size*N then downscales: stems survive to 8px
# instead of thresholding away. Offsets/advances below are divided back.
ss = int(args[args.index('--supersample') + 1]) if '--supersample' in args else 1
if ss > 1:
    font = ImageFont.truetype(path, size * ss)
codes = None
if '--codes' in args:
    spec = args[args.index('--codes') + 1]
    codes = []
    for part in spec.split(','):
        part = part.strip()
        if not part:
            continue
        if '-' in part:
            a, b = part.split('-', 1)
            codes.extend(range(int(a), int(b) + 1))
        else:
            codes.append(int(part))
ascent = font.getmetrics()[0]
if codes is None:
    codes = list(range(32, 127))
want = set(codes)
first, last = codes[0], codes[-1]
for code in range(first, last + 1):
    if code not in want:
        glyphs.append((0, 0, 0, 0, 0, 0))  # gap: empty glyph, keeps glyph[c-first] dense
        continue
    ch = chr(code)
    img = Image.new('L', (size * 4 + 64, size * 4 + 96), 0)
    if ss > 1:
        bigimg = Image.new('L', ((size * 4 + 64) * ss, (size * 4 + 96) * ss), 0)
        ImageDraw.Draw(bigimg).text((X0 * ss, Y0 * ss), ch, font=font, fill=255, anchor='ls')
        img = bigimg.resize((img.width, img.height), Image.BILINEAR)
    else:
        ImageDraw.Draw(img).text((X0, Y0), ch, font=font, fill=255, anchor='ls')
    adv = int(round(font.getlength(ch) / ss))
    bw = img.point(lambda p: 255 if p >= thresh else 0)
    box = bw.getbbox()
    off = len(bitmaps)
    if box is None:
        glyphs.append((off, 0, 0, adv, 0, 0))
        continue
    l, t, r, b = box
    w, h = r - l, b - t
    bits = []
    for y in range(t, b):
        for x in range(l, r):
            bits.append(1 if bw.getpixel((x, y)) else 0)
    while len(bits) % 8: bits.append(0)
    for i in range(0, len(bits), 8):
        bitmaps.append(int(''.join(str(v) for v in bits[i:i + 8]), 2))
    glyphs.append((off, w, h, adv, l - X0, t - Y0))

asc = max(-g[5] for g in glyphs if g[1])
desc = max(g[2] + g[5] for g in glyphs if g[1])
y_adv = asc + desc + 2
q = '' if sim else ' PROGMEM'
st = 'static ' if static else ''
lines = ['// %s at %dpx, converted from %s by ttf2gfx.py (ascent %d, descent %d, %d bytes of glyphs)' % (cname, size, path.split('\\')[-1].split('/')[-1], asc, desc, len(bitmaps))]
lines.append('#pragma once')
if sim: lines.append('#include "gfxfont.h"')
lines.append('%sconst uint8_t %sBitmaps[]%s = {' % (st, cname, q))
for i in range(0, len(bitmaps), 12):
    lines.append('  ' + ', '.join('0x%02X' % b for b in bitmaps[i:i + 12]) + ',')
lines.append('};')
lines.append('%sconst GFXglyph %sGlyphs[]%s = {' % (st, cname, q))
for i, g in enumerate(glyphs):
    cp = first + i
    try:
        cm = repr(chr(cp))
    except ValueError:
        cm = "'?'"
    lines.append('  { %5d, %3d, %3d, %3d, %4d, %4d },   // U+%04X %s' % (g + (cp, cm)))
lines.append('};')
lines.append('%sconst GFXfont %s%s = { (uint8_t*)%sBitmaps, (GFXglyph*)%sGlyphs, 0x%X, 0x%X, %d };' % (st, cname, q, cname, cname, first, last, y_adv))
open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
print('%s: ascent %d descent %d, %d glyph bytes' % (cname, asc, desc, len(bitmaps)))
