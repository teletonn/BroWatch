// SquachWatch-CYD — Squachy's diary screen implementation
#include "ui_diary.h"
#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include "type_names.h"
#include <Arduino.h>

// Stamped in by extra_script.py from `git describe` at build time — see
// platformio.ini. Falls back if that step is somehow skipped.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

void uiDiaryInit(TFT_eSPI& t) {
    // fillScreen() relies on TFT_eSPI's base-class width/height, which
    // TFT_eSprite::createSprite() never updates — it leaves stale
    // remnants of whatever screen was drawn before when t is a sprite.
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

static void formatDuration(uint32_t ms, char* buf, size_t n) {
    uint32_t sec  = ms / 1000;
    uint32_t days = sec / 86400; sec %= 86400;
    uint32_t hrs  = sec / 3600;  sec %= 3600;
    uint32_t mins = sec / 60;
    const bool ru = Settings::lang() == 1;
    if (days > 0)      snprintf(buf, n, ru ? "%lуд %lуч" : "%lud %luh", (unsigned long)days, (unsigned long)hrs);
    else if (hrs > 0)  snprintf(buf, n, ru ? "%луч %лум" : "%luh %lum", (unsigned long)hrs, (unsigned long)mins);
    else if (mins > 0) snprintf(buf, n, ru ? "%лум" : "%lum", (unsigned long)mins);
    else               snprintf(buf, n, ru ? "<1м" : "<1m");
}

static void drawStat(TFT_eSPI& t, int w, int y, int h, const char* label, const char* value) {
    t.setTextSize(1);
    t.setTextColor(Theme::CYAN, Theme::BG);
    t.setCursor(8, y + (h - t.fontHeight(1)) / 2);
    Theme::printRU(t, label);
    t.setTextColor(Theme::WHITE, Theme::BG);
    int vw = Theme::textWidthRU(t, value);
    t.setCursor(w - 8 - vw, y + (h - t.fontHeight(1)) / 2);
    Theme::printRU(t, value);
    t.drawFastHLine(4, y + h - 1, w - 8, Theme::PURPLE);
}

void uiDiaryTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    int w = t.width();

    char title[32];
    snprintf(title, sizeof(title), ">> %s'S DIARY <<", Squachy::nickname());
    Theme::drawTitleBar(t, title);

    const int top  = 16;
    // 22 rather than 24: nine rows plus the hint is 226px, which fits a
    // 240px panel. At 24 the ninth row pushed the hint off the bottom.
    const int rowH = 22;
    // 32, not 24: a RU type name is UTF-8 (2 bytes a glyph), so
    // "СМАРТТАГ 4294967295" needs the room.
    char buf[32];

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)eng.lifetimeTotal());
    drawStat(t, w, top + 0 * rowH, rowH, Theme::tr("LIFETIME CATCHES", "ВСЕГО ПОЙМАНО"), buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)Squachy::bootCount());
    drawStat(t, w, top + 1 * rowH, rowH, Theme::tr("BOOTS", "ЗАГРУЗОК"), buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)Squachy::petCount());
    drawStat(t, w, top + 2 * rowH, rowH, Theme::tr("TIMES PETTED", "ГЛАДИЛИ РАЗ"), buf);

    formatDuration(Squachy::currentClearStreakMs(), buf, sizeof(buf));
    drawStat(t, w, top + 3 * rowH, rowH, Theme::tr("CURRENT CLEAR STREAK", "СЕРИЯ БЕЗ НАХОДОК"), buf);

    formatDuration(Squachy::bestClearStreakMs(), buf, sizeof(buf));
    drawStat(t, w, top + 4 * rowH, rowH, Theme::tr("BEST CLEAR STREAK", "ЛУЧШАЯ СЕРИЯ"), buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)Squachy::bestSessionCount());
    drawStat(t, w, top + 5 * rowH, rowH, Theme::tr("BEST SESSION CATCH", "РЕКОРД ЗА РАЗ"), buf);

    DetectionType ft = Squachy::firstDetectionType();
    drawStat(t, w, top + 6 * rowH, rowH, Theme::tr("FIRST EVER CATCH", "ПЕРВАЯ ДОБЫЧА"),
             ft == DetectionType::UNKNOWN ? Theme::tr("none yet", "пока пусто") : TypeNames::display(ft));

    // Whichever type you have logged most, ever. The per-type lifetime
    // counters behind this persist independently of the live counts, which
    // decay as detections go stale -- so this answers "what do I actually
    // keep running into", which a session counter never could.
    {
        DetectionType best = DetectionType::UNKNOWN;
        uint32_t bestN = 0;
        for (uint8_t i = 1; i < (uint8_t)DetectionType::COUNT; i++) {
            const uint32_t n = eng.lifetimeTypeCount((DetectionType)i);
            if (n > bestN) { bestN = n; best = (DetectionType)i; }
        }
        if (bestN == 0) {
            drawStat(t, w, top + 7 * rowH, rowH, Theme::tr("MOST CAUGHT", "ЧАЩЕ ВСЕХ"), Theme::tr("none yet", "пока пусто"));
        } else {
            snprintf(buf, sizeof(buf), "%s %lu", TypeNames::display(best),
                     (unsigned long)bestN);
            drawStat(t, w, top + 7 * rowH, rowH, Theme::tr("MOST CAUGHT", "ЧАЩЕ ВСЕХ"), buf);
        }
    }

    drawStat(t, w, top + 8 * rowH, rowH, Theme::tr("FIRMWARE", "ПРОШИВКА"), FIRMWARE_VERSION);

    // Hint, pulsing gently so it doesn't just look like inert label text.
    // Between 65% and 100% of CYAN: the old 10-70% of VAPOR_BLUE rounded to
    // nearly nothing in the frame buffer, in every theme.
    float pulse = 0.825f + 0.175f * sinf((float)(now % 1600) / 1600.0f * 6.2831853f);
    uint16_t col = Theme::blend(Theme::BG, Theme::CYAN, (uint16_t)(pulse * 255.0f));
    t.setTextSize(1);
    t.setTextColor(col, Theme::BG);
    const char* hint = Theme::tr("tap anywhere to go back", "жми куда угодно — назад");
    int hw = Theme::textWidthRU(t, hint);
    t.setCursor((w - hw) / 2, top + 9 * rowH + 8);
    Theme::printRU(t, hint);
}
