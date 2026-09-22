// The QWERTY keyboard's geometry, hit test and touch filter.
//
// Two of these cases are regressions with names. The first mockup drew
// backspace on top of M. Its successor drew them apart and then gave wide
// keys a head start in the hit test that reached back into M anyway -- a key
// you could see you were pressing, resolving to the one beside it. Both are
// pinned below at both rotations, and the second is checked the only way that
// proves it: every pixel inside every key.
//
// The touch filter is here because it is the half a browser mockup cannot
// show. A pointer event hands over a clean coordinate on release; a resistive
// panel does not.
#include "qwerty.h"
#include "test_util.h"
#include <cstdio>
#include <cstring>

using namespace Qwerty;

struct Screen { int w, h; const char* name; };
static const Screen SCREENS[] = { {320, 240, "landscape"}, {240, 320, "portrait"} };

static int find(const Key* k, uint8_t n, char ch) {
    for (uint8_t i = 0; i < n; i++) if (k[i].ch == ch) return i;
    return -1;
}
static bool overlap(const Key& a, const Key& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

int main() {
    Key k[KEY_N];
    char msg[96];

    for (int bd = 0; bd < 2; bd++)
    for (int ext = 0; ext < 2; ext++)
    for (const Screen& s : SCREENS) {
        const Board board = bd ? Board::RU : Board::EN;
        const char* bname = bd ? "RU" : "EN";
        // The band the device actually gives the keyboard -- for a name, and
        // for a message, which adds a digit row and punctuation.
        const int top = BAND_TOP, bot = s.h - BAND_BOTTOM_INSET;
        const uint8_t n = layout(s.w, top, bot, k, ext != 0, board);
        // The key backspace follows: M on the name board, the apostrophe
        // after M on the message board. On the Russian board DEL sits on the
        // bottom row instead, so `mi` there is just a letter on the third.
        const char* kind = ext ? "message" : "name";
        const int mi = find(k, n, bd ? (char)(RU_BASE + RU_N - 1) : (ext ? '\'' : 'M'));
        const int bi = find(k, n, BKSP);

        snprintf(msg, sizeof msg, "The board is complete (%s, %s, %s)", s.name, kind, bname); suite(msg);
        {
            ck(bd ? (ext ? "fifty-one keys" : "thirty-six keys")
                  : (ext ? "forty-six keys" : "thirty keys"),
               n == (ext ? (bd ? 51 : 46) : (bd ? 36 : 30)));
            if (ext) {
                bool extra = true;
                // The Russian message board drops '-' for DEL on the bottom row.
                for (const char* c = bd ? "0123456789.,?!'" : "0123456789.,?!'-"; *c; c++) {
                    int count = 0;
                    for (uint8_t i = 0; i < n; i++) if (k[i].ch == *c) count++;
                    if (count != 1) extra = false;
                }
                ck("every digit and mark exactly once", extra);
            }
            if (!bd) {
                bool letters = true;
                for (char c = 'A'; c <= 'Z'; c++) {
                    int count = 0;
                    for (uint8_t i = 0; i < n; i++) if (k[i].ch == c) count++;
                    if (count != 1) letters = false;
                }
                ck("every letter exactly once", letters);
                bool noRu = true;
                for (uint8_t i = 0; i < n; i++) if (isRuKey(k[i].ch)) noRu = false;
                ck("no Russian id leaks onto the English board", noRu);
            } else {
                bool letters = true, noEn = true;
                for (uint8_t r = 0; r < RU_N; r++) {
                    int count = 0;
                    for (uint8_t i = 0; i < n; i++) if (k[i].ch == (char)(RU_BASE + r)) count++;
                    if (count != 1) letters = false;
                }
                for (char c = 'A'; c <= 'Z'; c++)
                    for (uint8_t i = 0; i < n; i++) if (k[i].ch == c) noEn = false;
                ck("every Russian letter exactly once", letters);
                ck("no Latin letter on the Russian board", noEn);
                ck("'-' gave its slot to DEL", find(k, n, '-') < 0);
                // The Russian board types transliteration, and the air
                // alphabet is Latin: every tail must fit it, apostrophe
                // included (both signs land on it).
                bool trOk = true;
                for (uint8_t r = 0; r < RU_N; r++) {
                    const char* tr = ruTr(r);
                    if (!tr[0]) trOk = false;
                    for (const char* p = tr; *p; p++)
                        if (!((*p >= 'A' && *p <= 'Z') || *p == '\'')) trOk = false;
                }
                ck("transliteration is air-charset only", trOk);
            }
            ck("space",     find(k, n, ' ') >= 0);
            ck("backspace", bi >= 0);
            // The message board clears; the name board shuffles the curated
            // name instead, in the same slot (v1.7.8).
            if (ext) ck("clear",   find(k, n, CLR)  >= 0);
            else     ck("shuffle", find(k, n, SHUF) >= 0);
            ck("OK",        find(k, n, OK)  >= 0);
        }

        snprintf(msg, sizeof msg, "Every key is on screen, in its band (%s, %s, %s)", s.name, kind, bname); suite(msg);
        {
            bool inside = true, sane = true;
            for (uint8_t i = 0; i < n; i++) {
                if (k[i].x < 0 || k[i].x + k[i].w > s.w ||
                    k[i].y < top || k[i].y + k[i].h > bot) inside = false;
                if (k[i].w < 18 || k[i].h < 18) sane = false;
            }
            ck("inside the screen and inside the band", inside);
            ck("no key under 18px in either direction", sane);
        }

        snprintf(msg, sizeof msg, "No two keys overlap (%s, %s, %s)", s.name, kind, bname); suite(msg);
        {
            bool clean = true;
            for (uint8_t i = 0; i < n; i++)
                for (uint8_t j = i + 1; j < n; j++)
                    if (overlap(k[i], k[j])) clean = false;
            ck("every pair of keys is disjoint", clean);
        }

        if (!bd) {
            snprintf(msg, sizeof msg, "Backspace does not cover the key before it (%s, %s)", s.name, kind); suite(msg);
            if (mi >= 0 && bi >= 0) {
                const Key& m = k[mi];
                const Key& b = k[bi];
                ck("they share a row",               m.y == b.y && m.h == b.h);
                ck("backspace starts after M ends",  b.x >= m.x + m.w);
                ck("with the full gap between them", b.x - (m.x + m.w) == BKSP_GAP);
            } else {
                ck("M and backspace both exist", false);
            }
        } else {
            snprintf(msg, sizeof msg, "DEL lives on the bottom row (%s, %s)", s.name, kind); suite(msg);
            const int oi = find(k, n, OK);
            if (mi >= 0 && bi >= 0 && oi >= 0) {
                const Key& m = k[mi];
                const Key& b = k[bi];
                const Key& o = k[oi];
                ck("below the letter rows",          b.y > m.y + m.h);
                ck("sharing the bottom row with OK", b.y == o.y && b.h == o.h);
            } else {
                ck("last letter, DEL and OK all exist", false);
            }
        }

        snprintf(msg, sizeof msg, "What you see is what you press (%s, %s, %s)", s.name, kind, bname); suite(msg);
        {
            // Every pixel of every key. Not a sample -- the bug this replaces
            // was a few columns wide, at one edge, of one key.
            long wrong = 0;
            for (uint8_t i = 0; i < n; i++)
                for (int y = k[i].y; y < k[i].y + k[i].h; y++)
                    for (int x = k[i].x; x < k[i].x + k[i].w; x++)
                        if (keyAt(k, n, x, y, 0) != i) wrong++;
            ck("every pixel inside a key resolves to that key", wrong == 0);

            // The English board's gutter geometry, pinned pixel by pixel. The
            // Russian board's DEL sits on another row, so its gutters answer
            // to the suite below instead.
            if (!bd && mi >= 0 && bi >= 0) {
                const Key& m = k[mi];
                const int midY = m.y + m.h / 2;
                ck("M's last column is M",
                   keyAt(k, n, m.x + m.w - 1, midY, 0) == mi);
                ck("one pixel into the gutter is still M",
                   keyAt(k, n, m.x + m.w, midY, 8) == mi);
                ck("the pixel just before backspace is backspace",
                   keyAt(k, n, m.x + m.w + BKSP_GAP - 1, midY, 8) == bi);
            }
        }

        snprintf(msg, sizeof msg, "The gutters are live (%s, %s, %s)", s.name, kind, bname); suite(msg);
        {
            // Nearest-rectangle means a press between two keys is not a press
            // on nothing. Anywhere inside the keyboard's outline resolves.
            int x0 = s.w, y0 = s.h, x1 = 0, y1 = 0;
            for (uint8_t i = 0; i < n; i++) {
                if (k[i].x < x0) x0 = k[i].x;
                if (k[i].y < y0) y0 = k[i].y;
                if (k[i].x + k[i].w > x1) x1 = k[i].x + k[i].w;
                if (k[i].y + k[i].h > y1) y1 = k[i].y + k[i].h;
            }
            long dead = 0;
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                    if (keyAt(k, n, x, y, 6) < 0) dead++;
            ck("no dead pixel inside the keyboard's outline", dead == 0);
            ck("a press well clear of it resolves to nothing",
               keyAt(k, n, s.w / 2, top - 30, 6) < 0);
        }
    }

    suite("Any reasonable band, not just today's");
    {
        // The screens above are the device as it is now. The invariants are
        // meant to hold for whatever band the readout and BACK end up leaving,
        // so sweep it rather than trust two numbers.
        bool ok = true;
        for (const Screen& s : SCREENS)
            for (int top = 30; top <= 70; top += 4)
                for (int bot = s.h - 60; bot <= s.h - 20; bot += 4) {
                  for (int ext = 0; ext < 2; ext++) {
                  for (int bd = 0; bd < 2; bd++) {
                    const Board board = bd ? Board::RU : Board::EN;
                    const uint8_t n = layout(s.w, top, bot, k, ext != 0, board);
                    if (n != (ext ? (bd ? 51 : 46) : (bd ? 36 : 30))) ok = false;
                    for (uint8_t i = 0; i < n; i++) {
                        if (k[i].x < 0 || k[i].x + k[i].w > s.w || k[i].y < 0) ok = false;
                        for (uint8_t j = i + 1; j < n; j++) if (overlap(k[i], k[j])) ok = false;
                    }
                    const int bi = find(k, n, BKSP);
                    if (bi < 0) ok = false;
                    if (!bd) {
                        const int mi = find(k, n, ext ? '\'' : 'M');
                        if (mi < 0 || k[bi].x - (k[mi].x + k[mi].w) != BKSP_GAP) ok = false;
                    } else {
                        const int mi = find(k, n, (char)(RU_BASE + RU_N - 1));
                        if (mi < 0 || k[bi].y <= k[mi].y + k[mi].h) ok = false;
                    }
                  }
                  }
                }
        ck("on screen, disjoint, and backspace placed throughout", ok);
    }

    suite("A single wild sample on release is ignored");
    {
        TouchFilter f;
        f.down(100, 100);
        f.move(102, 101);
        ck("an ordinary slide is followed", f.x == 102 && f.y == 101);
        f.move(260, 20);                          // pressure dropping
        ck("one wild sample does not move it", f.x == 102 && f.y == 101);
    }

    suite("A bad press sample does not lock the anchor");
    {
        // The press itself lied: the finger is really at 200,150.
        TouchFilter f;
        f.down(20, 20);
        f.move(200, 150);
        f.move(201, 151);
        ck("two agreeing samples are not enough yet", f.x == 20 && f.y == 20);
        f.move(202, 150);
        ck("the third moves the anchor to the finger", f.x == 202 && f.y == 150);
        f.move(205, 152);
        ck("and from there it follows normally", f.x == 205 && f.y == 152);
    }

    suite("Wild samples that disagree never add up");
    {
        TouchFilter f;
        f.down(100, 100);
        f.move(300, 10);
        f.move(10, 230);
        f.move(300, 200);
        f.move(20, 20);
        ck("scattered garbage leaves the anchor alone", f.x == 100 && f.y == 100);
    }

    suite("Transliteration: UTF-8 in, air-charset out");
    {
        // What a RU FILL opening becomes on its way to the keyboard: the
        // blank at the end survives, so the ending lands straight after it.
        char out[64];
        ck("opening keeps its tail space",
           transliterateRu("ВСТРЕЧА В ", out, sizeof out) == 11 &&
           strcmp(out, "VSTRECHA V ") == 0);
        ck("lower case lands on upper tails, Ё rides on Е",
           transliterateRu("Щука ёж", out, sizeof out) == 11 &&
           strcmp(out, "SHCHUKA EZH") == 0);
        ck("soft and hard signs land on the apostrophe",
           transliterateRu("съешь", out, sizeof out) == 6 &&
           strcmp(out, "S'ESH'") == 0);
        ck("latin lower-cases up, marks pass",
           transliterateRu("Meet at 5-a'b,c?!.-", out, sizeof out) == 19 &&
           strcmp(out, "MEET AT 5-A'B,C?!.-") == 0);
        ck("three-byte letters and emoji are dropped whole",
           transliterateRu("А€Б😀В", out, sizeof out) == 3 &&
           strcmp(out, "ABV") == 0);
        ck("broken bytes never stall",
           transliterateRu("А\xFF" "Б\xD0", out, sizeof out) == 2 &&
           strcmp(out, "AB") == 0);
        char tiny[4];
        ck("a mid-tail cutoff still NUL-terminates",
           transliterateRu("ЩУКА", tiny, sizeof tiny) == 3 &&
           strcmp(tiny, "SHC") == 0);
        ck("empty in, empty out",
           transliterateRu("", out, sizeof out) == 0 && out[0] == '\0');
        ck("zero cap writes nothing",
           transliterateRu("АБ", out, 0) == 0);
    }

    return report();
}
