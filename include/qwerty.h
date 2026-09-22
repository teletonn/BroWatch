// SquachWatch-CYD — the QWERTY keyboard's geometry, hit test and touch
// filter. Pure arithmetic: no display, no touch driver, which is the whole
// reason it is a file of its own -- test/qwerty_test.cpp runs it on a desktop
// against every pixel of both screen rotations.
//
// The payphone keypad in ui_phone.cpp stays the default. This is the bailout
// for people who hate multi-tap, and it is drawn by that same screen because
// the name being typed lives there: switching layouts mid-word keeps what you
// have typed, which is the entire point of a bailout.
#pragma once
#if SQUACH_MESH
#include <stdint.h>
#include <stddef.h>

namespace Qwerty {

// Control keys, as characters that can never be typed. Letters are 'A'..'Z'
// and space is ' '; everything else on the board is one of these.
constexpr char BKSP = '\b';
constexpr char CLR  = 0x18;   // ASCII CAN
constexpr char SHUF = 0x19;   // name board only: clear, and pick the next curated name
constexpr char OK   = '\n';

// The Russian board's letters, as key ids that can never be typed either.
// A Cyrillic letter is two bytes in UTF-8 and the mesh air alphabet is Latin
// anyway, so the key carries an id (RU_BASE + index into RU_GLYPH / RU_TR)
// while the label is drawn from RU_GLYPH and the press appends RU_TR.
constexpr char    RU_BASE = (char)0x80;
constexpr uint8_t RU_N    = 32;    // Й..Ъ in row order, no Ё: Е doubles for it,
// the way Russians type it everywhere else
inline bool isRuKey(char c) { return c <= (char)0x9F && c >= RU_BASE; }
// The UTF-8 glyph for RU_BASE + i, and what typing it appends: plain ASCII
// transliteration, every byte inside MeshMsg::TEXT_CHARSET, so a Russian
// board keeps the keyboards' promise that an untypeable text cannot exist.
const char* ruGlyph(uint8_t i);
const char* ruTr(uint8_t i);
// UTF-8 in, air-charset out: Cyrillic through RU_TR (lower case lands on the
// same upper-case tails the air can carry, Ё/ё ride on Е), ASCII letters
// upper-cased, digits and ` .,?!'-` passed through, everything else dropped.
// Spaces survive, so a FILL opening keeps the blank at its end. Always
// NUL-terminates; returns the bytes written, without the NUL.
size_t transliterateRu(const char* src, char* dst, size_t cap);

struct Key { int16_t x, y, w, h; char ch; };

// 26 letters, space, backspace, clear, OK -- plus the Russian board's 32,
// which is why the room.
constexpr uint8_t KEY_N = 56;   // the message board's 51, with room

// Which alphabet the board shows. EN is the QWERTY the firmware always had;
// RU is ЙЦУКЕН in row order (11/11/10 -- Cyrillic has 33 letters against
// Latin's 26, so its rows run eleven wide, the phone-keyboard standard) with
// the same digits, punctuation and controls around it.

// The gap between the last letter of the bottom row and backspace. A number
// of its own rather than the ordinary key gap, because that is exactly where
// the first mockup went wrong: backspace was drawn at a fixed x and landed on
// top of M. The test pins it at both rotations.
constexpr int BKSP_GAP = 6;

// The band ui_phone.cpp hands the keyboard: below the readout, and above the
// BACK row (26 tall, 6 off the bottom edge) plus 6 of air. Defined here so the
// test checks the geometry the device actually draws; ui_phone.cpp
// static_asserts that its BACK row still agrees.
constexpr int BAND_TOP          = 46;
constexpr int BAND_BOTTOM_INSET = 26 + 6 + 6;

enum class Board : uint8_t { EN, RU };

// Fills `out` with every key for a screen `w` pixels wide, fitted into the
// band [bandTop, bandBottom). Returns the number written.
//
// Rows are un-staggered -- rows two and three share a left edge -- because
// QWERTY's offset is a typewriter linkage artefact, and squaring it up is
// where the middle row gets its extra width.
// `message`: the board for a SquachMesh message -- a digit row on top, an
// apostrophe after M, and , . ? ! - around the space bar. A name gets the
// four-row letters-only board. The RU message board drops '-' for DEL, which
// moves to the bottom row: 32 letters do not leave room for backspace beside
// the third row the way 26 do.
uint8_t layout(int w, int bandTop, int bandBottom, Key out[KEY_N],
               bool message = false, Board board = Board::EN);

// The key nearest (x, y), measured to its RECTANGLE rather than its centre.
//
// That distinction is the fix for a real bug. Inside a key the distance is
// zero, so a pixel you can see is inside M always resolves to M and no
// neighbour can reach in, however wide it is. Between keys it splits the
// gutter at its midline. Returns -1 if every key is further than `reach`.
int keyAt(const Key* keys, uint8_t n, int x, int y, int reach);

// Resistive touch is honest while the finger is pressed and a liar on the way
// up: as pressure drops, the last sample or two can land anywhere on the
// panel. So the keyboard commits the last GOOD position, never the last one,
// and this is what decides which is which.
//
// A single sample that jumps further than a finger moves in one frame is
// ignored. A run of samples that agree with each other is accepted even
// though it jumped -- otherwise one bad sample on the press itself would lock
// the anchor somewhere the finger never was.
struct TouchFilter {
    static constexpr int     JUMP_PX  = 40;  // beyond this in one frame is not a hand
    static constexpr int     AGREE_PX = 8;   // how close consecutive jumped samples stay
    static constexpr uint8_t AGREE_N  = 3;   // how many of them it takes

    int16_t x = 0, y = 0;     // the last good position
    int16_t cx = 0, cy = 0;   // the latest sample that jumped
    uint8_t cn = 0;           // how many jumped samples in a row agree

    void down(int px, int py);
    void move(int px, int py);
};

} // namespace Qwerty
#endif
