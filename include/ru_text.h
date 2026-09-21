// BroWatch — Russian text helpers (i18n stage 1).
//
// Pure UTF-8 decoding + codepoint routing, no display dependency so the
// host tests can compile this directly. Glyphs themselves live in
// ru_font.h (RuCyr8, U+0401..U+0451); ASCII stays on the built-in GLCD
// font. Wiring into Theme print paths is i18n stage 2.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace RuText {

// Cyrillic block covered by RuCyr8 (see tools/ttf2gfx.py --codes).
static const uint16_t CYR_FIRST = 0x0401;   // Ё
static const uint16_t CYR_LAST  = 0x0451;   // ё (gaps inside are empty glyphs)

inline bool isCyrillic(uint16_t cp) { return cp >= CYR_FIRST && cp <= CYR_LAST; }
inline bool isAscii(uint16_t cp)    { return cp >= 0x20 && cp < 0x7F; }

// Decode one UTF-8 sequence at *p, advance *p past it. Invalid bytes yield
// '?' and advance one byte, so a broken string can never hang the UI.
inline uint16_t next(const char** p) {
    const uint8_t* s = (const uint8_t*)*p;
    uint8_t a = s[0];
    uint16_t cp;
    uint8_t n;
    if (a < 0x80) {
        *p += 1;
        return a;
    } else if ((a & 0xE0) == 0xC0) {
        cp = (uint16_t)(a & 0x1F);
        n = 1;
    } else if ((a & 0xF0) == 0xE0) {
        cp = (uint16_t)(a & 0x0F);
        n = 2;
    } else if ((a & 0xF8) == 0xF0) {
        cp = (uint16_t)(a & 0x07);
        n = 3;
    } else {
        *p += 1;
        return (uint16_t)'?';
    }
    for (uint8_t i = 0; i < n; i++) {
        uint8_t b = s[1 + i];
        if ((b & 0xC0) != 0x80) {
            *p += 1;
            return (uint16_t)'?';
        }
        cp = (uint16_t)((cp << 6) | (b & 0x3F));
    }
    *p += 1 + n;
    // Non-BMP does not fit uint16_t and has no glyphs here anyway.
    // Whitespace controls pass through untouched so printRU() can stand in
    // for print() anywhere; anything else down here becomes '?'.
    if (cp == '\n' || cp == '\r' || cp == '\t') return cp;
    if (cp < 0x20) return (uint16_t)'?';
    return cp;
}

// Glyph count, not byte count: what wrapText/layout must measure.
// (UTF-8 Cyrillic is 2 bytes per letter; strlen lies by 2x.)
inline uint16_t count(const char* s) {
    uint16_t c = 0;
    while (s && *s) {
        next(&s);
        c++;
    }
    return c;
}

}  // namespace RuText
