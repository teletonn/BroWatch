// SquachWatch-CYD — the bingo card screen. See ui_bingo.h.
#include "ui_bingo.h"
#include "bingo.h"
#include "theme.h"
#include "settings.h"
#include "detection_info.h"
#include "type_names.h"
#include "clock.h"
#include <Arduino.h>

namespace {

const int TOP = 16;
bool    s_stats    = false;
bool    s_confirm  = false;   // the "throw this card away?" panel is up
int8_t  s_openCell = -1;      // the square whose paragraph is up, or -1

// The card fills the space between the title bar and the button bar. Four
// squares across either way round: in portrait they are narrow and tall, in
// landscape wide and short, and the icon is sized from whichever is smaller.
void geom(TFT_eSPI& t, int w, int h, int& gx, int& gy, int& cw, int& ch) {
    const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    const int top    = TOP + 14;
    const int bottom = bar.y - 6;
    const int pad    = 6, gap = 4;
    cw = (w - 2 * pad - 3 * gap) / 4;
    ch = (bottom - top - 3 * gap) / 4;
    gx = (w - (4 * cw + 3 * gap)) / 2;
    gy = top;
    (void)t;
}

// The counters' short names, not detectionTypeName()'s: "SAMSUNG_TAG" and
// "EVIL TWIN" do not fit a quarter of a 240px screen, and the squares are
// the same four-letter shorthand the main screen's counters use. RU takes
// the adapted cell codes from TypeNames.
const char* shortName(DetectionType t) {
    if (Settings::lang() == 1) return TypeNames::cellRu(t);
    switch (t) {
        case DetectionType::FLOCK:       return "FLOCK";
        case DetectionType::AXON:        return "AXON";
        case DetectionType::META:        return "GLASS";
        case DetectionType::SKIMMER:     return "SKIM";
        case DetectionType::MESH:        return "MESH";
        case DetectionType::AIRTAG:      return "TRACKER";
        case DetectionType::DRONE:       return "DRONE";
        case DetectionType::ALPR:        return "ALPR";
        case DetectionType::CAMERA:      return "CAM";
        case DetectionType::SAMSUNG_TAG: return "STAG";
        case DetectionType::GOOGLE_TAG:  return "GTAG";
        case DetectionType::TILE:        return "TILE";
        case DetectionType::RING:        return "RING";
        case DetectionType::DEAUTH:      return "DEAUTH";
        case DetectionType::EVILTWIN:    return "TWIN";
        case DetectionType::IBEACON:     return "BEACON";
        case DetectionType::HACKER:      return "HACK";
        default:                         return "?";
    }
}

const char* dayName(uint8_t d) {
    static const char* NAMES[8] = { "", "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    return d < 8 ? NAMES[d] : "";
}

void drawCard(TFT_eSPI& t, int w, int h) {
    int gx, gy, cw, ch;
    geom(t, w, h, gx, gy, cw, ch);

    // The count, where a card screen wants it: beside the heading rather
    // than under the grid, which is where the squares are.
    char head[40];
    const uint8_t lines = Bingo::linesCalled();
    if (Settings::lang() == 1)
        snprintf(head, sizeof head, "%u ИЗ 16   ЛИНИЙ: %u",
                 (unsigned)Bingo::markedCount(), (unsigned)lines);
    else
        snprintf(head, sizeof head, "%u OF 16   %u LINE%s",
                 (unsigned)Bingo::markedCount(), (unsigned)lines, lines == 1 ? "" : "S");
    Theme::drawListHeading(t, head, Bingo::markedCount() == Bingo::CELLS ? Theme::AMBER : Theme::CYAN);

    t.setTextSize(1);
    for (uint8_t i = 0; i < Bingo::CELLS; i++) {
        const int x = gx + (i % 4) * (cw + 4);
        const int y = gy + (i / 4) * (ch + 4);
        const DetectionType type = Bingo::typeAt(i);
        const bool got  = Bingo::marked(i);
        const bool line = got && Bingo::inCalledLine(i);

        const uint16_t edge = line ? Theme::AMBER : (got ? Theme::GREEN : Theme::PURPLE);
        t.fillRect(x, y, cw, ch, Theme::BG);
        t.drawRect(x, y, cw, ch, edge);

        // The type's own icon, the one the alert and the log draw. An
        // uncaught square wears it faded rather than empty: the card is a
        // list of things to go and find, so it has to show what they look
        // like before you have found one.
        const int s = (cw < ch ? cw : ch) / 2 - 5;
        if (s >= 4) {
            const int icy = y + ch / 2 - 4;
            Theme::drawTypeIcon(t, type, x + cw / 2, icy, s);
            // Not yet caught: the icon behind a scanline veil. The icons draw
            // from their own palette, so dimming the theme's does nothing to
            // them -- every other row of the square goes back to background
            // instead -- every third row, which reads as faded while leaving the
            // shape of the thing you are looking for.
            if (!got)
                for (int ly = y + 2; ly < y + ch - 2; ly += 3)
                    t.drawFastHLine(x + 1, ly, cw - 2, Theme::BG);
        }

        const char* name = shortName(type);
        t.setTextColor(line ? Theme::AMBER : (got ? Theme::GREEN : Theme::W95_LIGHT), Theme::BG);
        int tw = Theme::textWidthRU(t, name);
        if (tw > cw - 4) tw = cw - 4;
        t.setCursor(x + (cw - tw) / 2, y + ch - t.fontHeight() - 2);
        Theme::printRU(t, name);

        if (got) {
            const char* d = dayName(Bingo::markDay(i));
            t.setTextColor(Theme::W95_SHADOW, Theme::BG);
            t.setCursor(x + cw - t.textWidth(d) - 2, y + 2);
            t.print(d);
        }
    }
}

void drawStats(TFT_eSPI& t, int w, int h) {
    Theme::drawListHeading(t, Theme::tr("BINGO STATS", "БИНГО: СЧЁТ"), Theme::VAPOR_PINK);
    const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);
    t.setTextSize(1);
    int y = TOP + 18;
    const int lineH = t.fontHeight() + 6;

    struct Row { const char* k; char v[24]; } rows[6];
    uint8_t n = 0;
    const bool ru = Settings::lang() == 1;
    snprintf(rows[n].v, sizeof rows[n].v, "%u", (unsigned)Bingo::cardsFilled()); rows[n++].k = ru ? "КАРТ ЗАКРЫТО" : "CARDS FILLED";
    if (ru) snprintf(rows[n].v, sizeof rows[n].v, "%u из 16", (unsigned)Bingo::bestFilled());
    else    snprintf(rows[n].v, sizeof rows[n].v, "%u of 16", (unsigned)Bingo::bestFilled());
    rows[n++].k = ru ? "ЛУЧШАЯ КАРТА" : "BEST CARD";
    if (ru) snprintf(rows[n].v, sizeof rows[n].v, "%u нед.", (unsigned)Bingo::streak());
    else    snprintf(rows[n].v, sizeof rows[n].v, "%u weeks", (unsigned)Bingo::streak());
    rows[n++].k = ru ? "СЕРИЯ" : "STREAK";
    if (ru) snprintf(rows[n].v, sizeof rows[n].v, "%u нед.", (unsigned)Bingo::bestStreak());
    else    snprintf(rows[n].v, sizeof rows[n].v, "%u weeks", (unsigned)Bingo::bestStreak());
    rows[n++].k = ru ? "ЛУЧШАЯ СЕРИЯ" : "BEST STREAK";
    snprintf(rows[n].v, sizeof rows[n].v, "%u", (unsigned)Bingo::linesEver()); rows[n++].k = ru ? "ЛИНИЙ ЗАКРЫТО" : "LINES CALLED";
    if (ru) snprintf(rows[n].v, sizeof rows[n].v, "%u из 16", (unsigned)Bingo::markedCount());
    else    snprintf(rows[n].v, sizeof rows[n].v, "%u of 16", (unsigned)Bingo::markedCount());
    rows[n++].k = ru ? "ЭТА КАРТА" : "THIS CARD";

    for (uint8_t i = 0; i < n && y + lineH < bar.y - 20; i++) {
        t.setTextColor(Theme::AMBER, Theme::BG);
        t.setCursor(10, y);
        Theme::printRU(t, rows[i].k);
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(w / 2 + 10, y);
        Theme::printRU(t, rows[i].v);
        y += lineH;
    }

    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    t.setCursor(10, y + 4);
    t.print(Bingo::weekNumber() ? "A fresh card every week." : "No clock yet: this card stands.");
}

// The "are you sure" panel behind NEW. A week's marks are the whole game, and
// NEW sits between two buttons people tap without looking -- so the tap asks
// first, and the safe answer is the wide one at the bottom, where a reflex
// tap lands (the same reasoning as the LOG's CANCEL).
void confirmRects(int screenW, int screenH, int& px, int& py, int& pw, int& ph,
                  int& yesX, int& yesY, int& yesW, int& yesH,
                  int& noX,  int& noY,  int& noW,  int& noH) {
    pw = screenW - 40;
    if (pw > 240) pw = 240;
    // Tall enough for three wrapped lines above the buttons: at 132 the last
    // line of the warning ran under DEAL A NEW ONE.
    ph = 164;
    px = (screenW - pw) / 2;
    py = (screenH - ph) / 2;
    const int margin = 10, gap = 8, btnH = 24;
    noY  = py + ph - btnH - margin;
    noH  = btnH;
    noX  = px + margin;
    noW  = pw - 2 * margin;
    yesY = noY - gap - btnH;
    yesH = btnH;
    yesX = px + margin;
    yesW = pw - 2 * margin;
}

void drawConfirm(TFT_eSPI& t, int w, int h) {
    int px, py, pw, ph, yesX, yesY, yesW, yesH, noX, noY, noW, noH;
    confirmRects(w, h, px, py, pw, ph, yesX, yesY, yesW, yesH, noX, noY, noW, noH);
    t.fillRoundRect(px, py, pw, ph, 6, Theme::BG);
    t.drawRoundRect(px, py, pw, ph, 6, Theme::PURPLE);

    t.setTextWrap(false);
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    const char* q = Theme::tr("THROW THIS CARD AWAY?", "ВЫКИНУТЬ КАРТУ?");
    t.setCursor(px + (pw - Theme::textWidthRU(t, q)) / 2, py + 8);
    Theme::printRU(t, q);

    char marked[24];
    if (Settings::lang() == 1) snprintf(marked, sizeof marked, "%u ИЗ 16", (unsigned)Bingo::markedCount());
    else                       snprintf(marked, sizeof marked, "%u OF 16 MARKED", (unsigned)Bingo::markedCount());
    const int maxLw = pw - 16;
    if (Settings::lang() == 1) {
        // Bangers has no Cyrillic: set the RU headline in font 1 instead.
        t.setTextSize(2);
        t.setTextColor(Theme::RED, Theme::BG);
        int mw2 = Theme::textWidthRU(t, marked);
        if (mw2 > maxLw) mw2 = maxLw;
        t.setCursor(px + (pw - mw2) / 2, py + 26);
        Theme::printRU(t, marked);
        t.setTextSize(1);
    } else {
        int lw = Theme::bangersTextWidth(marked, Theme::BangersSize::MD);
        if (lw > maxLw) lw = maxLw;
        Theme::drawBangersText(t, px + (pw - lw) / 2, py + 26, marked, Theme::RED, Theme::BangersSize::MD);
    }

    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    char lines[3][48];
    const uint8_t n = Theme::wrapTextRU(t, Theme::tr("A new card loses them, and the streak with them. Lines already called are kept.",
                                                     "Новая карта всё сбросит, и серию тоже. Вызванные линии останутся."),
                                      pw - 16, lines, 3);
    int ly = py + 50;
    for (uint8_t i = 0; i < n; i++) {
        t.setCursor(px + (pw - Theme::textWidthRU(t, lines[i])) / 2, ly);
        Theme::printRU(t, lines[i]);
        ly += 12;
    }

    Theme::drawButton(t, yesX, yesY, yesW, yesH, Theme::tr("DEAL A NEW ONE", "НОВАЯ КАРТА"), false);
    Theme::drawButton(t, noX,  noY,  noW,  noH,  Theme::tr("KEEP THIS ONE",  "ОСТАВИТЬ"), false);
}

}  // namespace

void uiBingoInit(TFT_eSPI& t) {
    s_stats    = false;
    s_confirm  = false;
    s_openCell = -1;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiBingoTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    const int w = t.width(), h = t.height();
    const Theme::ButtonBarGeom bar = Theme::computeButtonBar(w, h);

    Theme::Palette saved = Theme::dimPaletteForOverlay(179);
    Theme::drawActiveBackground(t, now, 0, h, eng, advance);
    Theme::restorePalette(saved);

    Theme::drawTitleBar(t, ">> BINGO <<");
    if (s_stats) drawStats(t, w, h);
    else         drawCard(t, w, h);

    Theme::drawButton(t, bar.x[0], bar.y, bar.w[0], bar.h, s_stats ? Theme::tr("[ CARD ]", "[ КАРТА ]") : Theme::tr("[ STATS ]", "[ СТАТ. ]"), false);
    Theme::drawButton(t, bar.x[1], bar.y, bar.w[1], bar.h, Theme::tr("[ NEW ]", "[ НОВАЯ ]"), s_confirm);
    // OK, not BACK: this leaves for the screen the board lives on, the way
    // Settings' own OK does, rather than stepping back into the menu.
    Theme::drawButton(t, bar.x[2], bar.y, bar.w[2], bar.h, Theme::tr("[ OK ]", "[ ОК ]"), false);

    // Over everything else, and last: the panel that was asked for.
    if (s_confirm) drawConfirm(t, w, h);
    else if (s_openCell >= 0) {
        const DetectionType type = Bingo::typeAt((uint8_t)s_openCell);
        Theme::drawInfoPanel(t, w, h, now, TypeNames::display(type), DetectionInfo::explain(type));
        // The panel covers the card, so the veil above it does not matter here.
    }
}

BingoTap uiBingoHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    if (s_confirm) {
        int px, py, pw, ph, yesX, yesY, yesW, yesH, noX, noY, noW, noH;
        confirmRects(screenW, screenH, px, py, pw, ph, yesX, yesY, yesW, yesH, noX, noY, noW, noH);
        if (x >= yesX && x <= yesX + yesW && y >= yesY && y <= yesY + yesH) {
            s_confirm = false;
            Bingo::newCard();
            return BingoTap::HANDLED;
        }
        // KEEP THIS ONE, and anywhere else: the card stays. A tap that misses
        // a button is a tap that did not mean to throw the week away.
        s_confirm = false;
        return BingoTap::HANDLED;
    }
    if (s_openCell >= 0) {
        // Anywhere outside it closes it, the same as every other panel.
        s_openCell = -1;
        (void)Theme::infoPanelHitDismiss(x, y, screenW, screenH);
        return BingoTap::HANDLED;
    }

    const Theme::ButtonBarGeom bar = Theme::computeButtonBar(screenW, screenH);
    if (y >= bar.y && y < bar.y + bar.h) {
        if (x >= bar.x[2]) return BingoTap::BACK;
        if (x >= bar.x[1]) { s_confirm = true; return BingoTap::HANDLED; }
        s_stats = !s_stats;
        return BingoTap::HANDLED;
    }
    if (s_stats) return BingoTap::NONE;

    int gx, gy, cw, ch;
    geom(t, screenW, screenH, gx, gy, cw, ch);
    for (uint8_t i = 0; i < Bingo::CELLS; i++) {
        const int cx = gx + (i % 4) * (cw + 4);
        const int cy = gy + (i / 4) * (ch + 4);
        if (x >= cx && x < cx + cw && y >= cy && y < cy + ch) {
            s_openCell = (int8_t)i;
            return BingoTap::HANDLED;
        }
    }
    return BingoTap::NONE;
}
