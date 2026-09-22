// SquachWatch-CYD — QWERTY geometry. See include/qwerty.h.
#include "qwerty.h"

#if SQUACH_MESH

namespace Qwerty {

namespace {
const int MARGIN  = 4;    // off each screen edge
const int GAP     = 2;    // between keys in a row
const int ROW_GAP = 4;    // between rows
const int ROW_MIN = 26, ROW_MAX = 40;

inline int iabs(int v) { return v < 0 ? -v : v; }

void put(Key* out, uint8_t& n, int x, int y, int w, int h, char ch) {
    out[n].x = (int16_t)x; out[n].y = (int16_t)y;
    out[n].w = (int16_t)w; out[n].h = (int16_t)h;
    out[n].ch = ch;
    n++;
}
} // namespace

// The Russian board: ЙЦУКЕН in row order, Ъ moved from the first row's end
// to the third (eleven-wide rows cannot hold the standard twelve), Ё folded
// into Е the way Russian digital orthography permits. Transliteration is the
// readable kind (SHCH, not SHH): what flies is Latin, what you see is yours.
static const char* const RU_GLYPH[RU_N] = {
    "Й","Ц","У","К","Е","Н","Г","Ш","Щ","З","Х",
    "Ф","Ы","В","А","П","Р","О","Л","Д","Ж","Э",
    "Я","Ч","С","М","И","Т","Ь","Б","Ю","Ъ",
};
static const char* const RU_TR[RU_N] = {
    "J","TS","U","K","E","N","G","SH","SHCH","Z","H",
    "F","Y","V","A","P","R","O","L","D","ZH","E",
    "YA","CH","S","M","I","T","'","B","YU","'",
};

// Public faces of the tables above: ui_phone.cpp draws labels from one and
// types transliteration from the other.
const char* ruGlyph(uint8_t i) { return i < RU_N ? RU_GLYPH[i] : "?"; }
const char* ruTr(uint8_t i)    { return i < RU_N ? RU_TR[i] : ""; }

// Unicode order А..Я into the tables' row order: the keyboard's rows are
// ЙЦУКЕН, the alphabet's are not.
static const uint8_t RU_FROM_UNI[32] = {
    14,29,13, 6,19, 4,20, 9,26, 0, 3,18,25, 5,17,15,
    16,24,27, 2,11,10, 1,23, 7, 8,31,12,28,21,30,22,
};

size_t transliterateRu(const char* src, char* dst, size_t cap) {
    if (!cap) return 0;
    size_t n = 0;
    const uint8_t* p = (const uint8_t*)(src ? src : "");
    while (*p) {
        if (*p < 0x80) {
            // The air is upper-case only; the marks it accepts pass through.
            char c = (char)*p++;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                            c == ' ' || c == '.' || c == ',' || c == '?' ||
                            c == '!' || c == '\'' || c == '-';
            if (ok && n + 1 < cap) dst[n++] = c;
        } else if ((p[0] & 0xE0) == 0xC0 && p[1]) {
            const unsigned cp = ((p[0] & 0x1FU) << 6) | (p[1] & 0x3FU);
            int idx = -1;
            if (cp >= 0x410U && cp <= 0x42FU)      idx = RU_FROM_UNI[cp - 0x410U];
            else if (cp >= 0x430U && cp <= 0x44FU) idx = RU_FROM_UNI[cp - 0x430U];
            else if (cp == 0x401U || cp == 0x451U) idx = 4;   // Ё/ё ride on Е
            if (idx >= 0)
                for (const char* s = RU_TR[idx]; *s && n + 1 < cap; s++) dst[n++] = *s;
            p += 2;
        } else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
            p += 3;   // no three-byte letter flies: skip it whole
        } else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            p += 4;
        } else {
            p++;      // a broken byte: skip, never stall
        }
    }
    dst[n] = '\0';
    return n;
}

uint8_t layout(int w, int bandTop, int bandBottom, Key out[KEY_N], bool message, Board board) {
    const int avail = w - 2 * MARGIN;

    // A message needs digits and punctuation that a name does not, so its
    // board has a fifth row -- the digits, on top, where every keyboard puts
    // them -- and a few more keys on the bottom two rows.
    const int rows = message ? 5 : 4;

    // Row height from the band, clamped. Portrait has far more height than a
    // keyboard wants, and a 54px key is not a better key, just a taller one.
    int rh = (bandBottom - bandTop - (rows - 1) * ROW_GAP) / rows;
    if (rh > ROW_MAX) rh = ROW_MAX;
    if (rh < ROW_MIN) rh = ROW_MIN;
    // Anchored to the bottom of the band, where BACK is and where the hand
    // already is, rather than floating mid-screen with a gap under it.
    int y = bandBottom - (rows * rh + (rows - 1) * ROW_GAP);

    uint8_t n = 0;

    // Rows 0 and 1: ten keys across the full width.
    const int w1 = (avail - 9 * GAP) / 10;
    const int x1 = MARGIN + (avail - (10 * w1 + 9 * GAP)) / 2;
    if (message) {
        static const char R0[] = "1234567890";
        for (int i = 0; i < 10; i++)
            put(out, n, x1 + i * (w1 + GAP), y, w1, rh, R0[i]);
        y += rh + ROW_GAP;
    }
    if (board == Board::RU) {
        // Eleven-wide letter rows on their own centred grid -- 32 letters
        // need the phone-keyboard standard the EN board's ten cannot hold.
        // Same rows, same band, same controls around them.
        const int w11 = (avail - 10 * GAP) / 11;
        const int x11 = MARGIN + (avail - (11 * w11 + 10 * GAP)) / 2;
        for (int i = 0; i < 11; i++)
            put(out, n, x11 + i * (w11 + GAP), y, w11, rh, (char)(RU_BASE + i));
        y += rh + ROW_GAP;
        for (int i = 0; i < 11; i++)
            put(out, n, x11 + i * (w11 + GAP), y, w11, rh, (char)(RU_BASE + 11 + i));
        // Third row: the last ten, and on the message board an apostrophe
        // after them -- then the bottom row, which is also where DEL lives on
        // this board: beside the third row there is no room left for it.
        const int y3 = y + rh + ROW_GAP;
        if (message) {
            for (int i = 0; i < 10; i++)
                put(out, n, x11 + i * (w11 + GAP), y3, w11, rh, (char)(RU_BASE + 22 + i));
            put(out, n, x11 + 10 * (w11 + GAP), y3, w11, rh, '\'');
        } else {
            for (int i = 0; i < 10; i++)
                put(out, n, x1 + i * (w1 + GAP), y3, w1, rh, (char)(RU_BASE + 22 + i));
        }
        const int w2r    = (avail - 8 * GAP) / 9;
        const int span2r = 9 * w2r + 8 * GAP;
        const int x2r    = MARGIN + (avail - span2r) / 2;
        const int owr    = w2r * 3 / 2;
        const int y4 = y3 + rh + ROW_GAP;
        int x = x2r;
        if (!message) {
            const int cw = w2r * 2;
            const int sw = span2r - 3 * cw - 3 * GAP;
            put(out, n, x, y4, cw, rh, SHUF); x += cw + GAP;
            put(out, n, x, y4, cw, rh, BKSP); x += cw + GAP;
            put(out, n, x, y4, sw, rh, ' ');  x += sw + GAP;
            put(out, n, x, y4, cw, rh, OK);
        } else {
            const int sw = span2r - 6 * w2r - owr - 7 * GAP;
            put(out, n, x, y4, w2r, rh, CLR);  x += w2r + GAP;
            put(out, n, x, y4, w2r, rh, BKSP); x += w2r + GAP;
            put(out, n, x, y4, w2r, rh, ',');  x += w2r + GAP;
            put(out, n, x, y4, w2r, rh, '.');  x += w2r + GAP;
            put(out, n, x, y4, sw,  rh, ' ');  x += sw + GAP;
            put(out, n, x, y4, w2r, rh, '?');  x += w2r + GAP;
            put(out, n, x, y4, w2r, rh, '!');  x += w2r + GAP;
            put(out, n, x, y4, owr, rh, OK);
        }
        return n;
    }
    static const char R1[] = "QWERTYUIOP";
    for (int i = 0; i < 10; i++)
        put(out, n, x1 + i * (w1 + GAP), y, w1, rh, R1[i]);

    // Row 2: nine keys over the same width, so each is wider. This is the
    // whole return on un-staggering.
    static const char R2[] = "ASDFGHJKL";
    const int w2    = (avail - 8 * GAP) / 9;
    const int span2 = 9 * w2 + 8 * GAP;
    const int x2    = MARGIN + (avail - span2) / 2;
    const int y2    = y + rh + ROW_GAP;
    for (int i = 0; i < 9; i++)
        put(out, n, x2 + i * (w2 + GAP), y2, w2, rh, R2[i]);

    // Row 3: seven letters on row two's grid -- and on the message board an
    // apostrophe after them -- then backspace in what is left.
    //
    // Backspace is placed AFTER the last key -- from where it actually ends,
    // plus its own gap -- rather than at a position chosen for it. The first
    // mockup did it the other way round, put backspace at a fixed x, and it
    // landed on top of M. Built in this order there is no x at which the two
    // can meet, whatever the screen width or however many keys come before.
    static const char R3[] = "ZXCVBNM";
    const int y3 = y2 + rh + ROW_GAP;
    for (int i = 0; i < 7; i++)
        put(out, n, x2 + i * (w2 + GAP), y3, w2, rh, R3[i]);
    const int keys3 = message ? 8 : 7;
    if (message) put(out, n, x2 + 7 * (w2 + GAP), y3, w2, rh, '\'');
    const int lastEnd = x2 + keys3 * w2 + (keys3 - 1) * GAP;   // one past the last column
    const int bx      = lastEnd + BKSP_GAP;
    put(out, n, bx, y3, (x2 + span2) - bx, rh, BKSP);

    // Row 4 on row two's span. Space takes what is left; the rest are sized
    // for what it costs to miss them.
    const int y4 = y3 + rh + ROW_GAP;
    if (!message) {
        // Shuffle, space, OK. Shuffle is where CLR was: it clears too, and
        // then lands on a curated name instead of on nothing.
        const int cw = w2 * 2, ow = w2 * 2;
        const int sw = span2 - cw - ow - 2 * GAP;
        put(out, n, x2,                       y4, cw, rh, SHUF);
        put(out, n, x2 + cw + GAP,            y4, sw, rh, ' ');
        put(out, n, x2 + cw + GAP + sw + GAP, y4, ow, rh, OK);
    } else {
        // Clear, the two marks that end most sentences' clauses, space, the
        // three that end the rest, OK. Space is still the widest key on the
        // board after OK -- 50px landscape, 38 portrait.
        const int ow = w2 * 3 / 2;
        const int sw = span2 - 6 * w2 - ow - 7 * GAP;
        int x = x2;
        put(out, n, x, y4, w2, rh, CLR); x += w2 + GAP;
        put(out, n, x, y4, w2, rh, ','); x += w2 + GAP;
        put(out, n, x, y4, w2, rh, '.'); x += w2 + GAP;
        put(out, n, x, y4, sw, rh, ' '); x += sw + GAP;
        put(out, n, x, y4, w2, rh, '?'); x += w2 + GAP;
        put(out, n, x, y4, w2, rh, '!'); x += w2 + GAP;
        put(out, n, x, y4, w2, rh, '-'); x += w2 + GAP;
        put(out, n, x, y4, ow, rh, OK);
    }

    return n;
}

int keyAt(const Key* keys, uint8_t n, int x, int y, int reach) {
    int best = -1;
    long bestD = (long)reach * reach + 1;
    for (uint8_t i = 0; i < n; i++) {
        const Key& k = keys[i];
        const int r = k.x + k.w - 1, b = k.y + k.h - 1;
        const int dx = (x < k.x) ? (k.x - x) : (x > r ? x - r : 0);
        const int dy = (y < k.y) ? (k.y - y) : (y > b ? y - b : 0);
        const long d = (long)dx * dx + (long)dy * dy;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void TouchFilter::down(int px, int py) {
    x = (int16_t)px; y = (int16_t)py;
    cn = 0;
}

void TouchFilter::move(int px, int py) {
    if (iabs(px - x) + iabs(py - y) <= JUMP_PX) {
        x = (int16_t)px; y = (int16_t)py;       // an ordinary slide
        cn = 0;
        return;
    }
    // A jump. One on its own is the panel losing contact; a run of them that
    // agree is the finger genuinely being somewhere else -- most likely
    // because the press sample itself was the bad one. Compared sample to
    // sample, so a finger that really is moving still accumulates.
    if (cn && iabs(px - cx) + iabs(py - cy) <= AGREE_PX) {
        cx = (int16_t)px; cy = (int16_t)py;
        if (++cn >= AGREE_N) { x = cx; y = cy; cn = 0; }
    } else {
        cx = (int16_t)px; cy = (int16_t)py;
        cn = 1;
    }
}

} // namespace Qwerty
#endif // SQUACH_MESH
