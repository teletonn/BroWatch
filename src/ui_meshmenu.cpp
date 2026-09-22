// SquachWatch-CYD — the SquachMesh menu. See include/ui_meshmenu.h.
#include "ui_meshmenu.h"

#if SQUACH_MESH

#include "theme.h"
#include "settings.h"
#include "squachy.h"
#include "detection.h"
#include "meshtalk.h"
#if MESH_COMPANION
#include "mesh_link.h"
#endif
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

namespace {

const int TOP_MARGIN = Theme::LIST_TOP + Theme::LIST_HEADING_H;   // under the heading

// The rows shown, in the order drawn. BROMESH is the seven it always was, with
// MODE now pinned on top; COMPANION replaces the SquachMesh switches with the
// node link's own four. Returns the count and fills out[].
uint8_t buildRows(MeshMenuRow* out) {
    uint8_t n = 0;
#if MESH_COMPANION
    out[n++] = MeshMenuRow::MODE;
    if (Settings::companionMode()) {
        out[n++] = MeshMenuRow::TARGET;
        out[n++] = MeshMenuRow::NODE;
        out[n++] = MeshMenuRow::CHANNELS;
        out[n++] = MeshMenuRow::CONTACTS;
        out[n++] = MeshMenuRow::CHAT;
        return n;
    }
#endif
    out[n++] = MeshMenuRow::DETECT;
    out[n++] = MeshMenuRow::TRANSMIT;
    out[n++] = MeshMenuRow::MESSAGES;
    out[n++] = MeshMenuRow::CROWD;
    out[n++] = MeshMenuRow::SQUAD;
    out[n++] = MeshMenuRow::PHRASE;
    out[n++] = MeshMenuRow::NAME;
    return n;
}

// Row height from live font metrics, shared by drawing and hit-testing so
// the two cannot drift -- the same reason every other row list in this
// codebase computes it rather than hard-coding it.
void geom(TFT_eSPI& t, int& top, int& rowH) {
    top = TOP_MARGIN;
    t.setTextSize(Theme::uiMenuTextSize(t));
    rowH = t.fontHeight() + 10;
}

void row(TFT_eSPI& t, int w, int y, int hgt, const char* label,
         const char* value, uint16_t valueCol) {
    // A solid panel under the row, as the settings screen has. Without it
    // the labels sat straight on the dimmed backdrop, and the synthwave sun
    // came through the gaps in every word.
    Theme::drawListRowPanel(t, w, y, hgt);
    t.setTextSize(Theme::uiMenuTextSize(t));
    t.setTextWrap(false);
    t.setTextColor(Theme::VAPOR_PINK, Theme::BG);
    t.setCursor(8, y + (hgt - t.fontHeight()) / 2);
    Theme::printRU(t, label);
    if (!value) return;
    t.setTextColor(valueCol, Theme::BG);
    const int vw = Theme::textWidthRU(t, value);
    t.setCursor(w - 8 - vw, y + (hgt - t.fontHeight()) / 2);
    Theme::printRU(t, value);
}

} // namespace

void uiMeshMenuInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshMenuTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance) {
    const int w = t.width(), h = t.height();

    // Same knocked-back backdrop the other sub-screens use, so this reads as
    // part of the device rather than as a dialog bolted on.
    Theme::Palette saved = Theme::dimPaletteForOverlay(150);
    Theme::drawActiveBackground(t, now, 0, h, eng, advance);
    Theme::restorePalette(saved);
    Theme::dimRegion(t, 0, 0, w, h, 128);

    Theme::drawListHeading(t, Theme::tr("BROMESH", "БРОМЕШ"), Theme::VAPOR_PINK);

    int top, rowH;
    geom(t, top, rowH);

    MeshMenuRow rows[8];
    const uint8_t n = buildRows(rows);

    for (uint8_t i = 0; i < n; i++) {
        const int y = top + i * rowH;
        switch (rows[i]) {
#if MESH_COMPANION
            case MeshMenuRow::MODE: {
                const bool comp = Settings::companionMode();
                row(t, w, y, rowH, Theme::tr("MODE", "РЕЖИМ"), Settings::companionModeLabel(),
                    comp ? Theme::VAPOR_YELLOW : Theme::W95_SHADOW);
            } break;
#endif
            case MeshMenuRow::DETECT:
                row(t, w, y, rowH, Theme::tr("DETECT", "СЛУШАТЬ"), Settings::meshDetectLabel(),
                    Settings::meshDetect() ? Theme::GREEN : Theme::W95_SHADOW);
                break;
            case MeshMenuRow::TRANSMIT:
                row(t, w, y, rowH, Theme::tr("TRANSMIT", "ВЕЩАТЬ"), Settings::meshTransmitLabel(),
                    Settings::meshTransmit() ? Theme::AMBER : Theme::W95_SHADOW);
                break;
            case MeshMenuRow::MESSAGES: {
                const bool st = MeshTalk::selfTestOk();
                row(t, w, y, rowH, Theme::tr("MESSAGES", "ПИСЬМА"),
                    !st ? Theme::tr("ERR", "ОШИБКА") : (Settings::messagesOn() ? Theme::tr("ON", "ВКЛ") : Theme::tr("OFF", "ВЫКЛ")),
                    !st ? Theme::RED : (Settings::messagesOn() ? Theme::GREEN : Theme::W95_SHADOW));
            } break;
            case MeshMenuRow::CROWD:
                row(t, w, y, rowH, Theme::tr("CROWD", "ТОЛПА"), Settings::meshCrowdLabel(),
                    Settings::meshCrowd() > 1 ? Theme::GREEN : Theme::W95_SHADOW);
                break;
            case MeshMenuRow::SQUAD: {
                char sq[16];
                const uint8_t members = MeshTalk::rosterCount();
                if (members) snprintf(sq, sizeof sq, "%u >", (unsigned)members);
                else         snprintf(sq, sizeof sq, "%s >", Theme::tr("NONE", "ПУСТО"));
                row(t, w, y, rowH, Theme::tr("SQUAD", "ОТРЯД"), sq, members ? Theme::GREEN : Theme::W95_SHADOW);
            } break;
            case MeshMenuRow::PHRASE:
                row(t, w, y, rowH, Theme::tr("PHRASE", "ФРАЗА"),
                    MeshTalk::havePhrase() ? Theme::tr("SET >", "ЕСТЬ >") : Theme::tr("NONE >", "ПУСТО >"),
                    MeshTalk::havePhrase() ? Theme::VAPOR_YELLOW : Theme::W95_SHADOW);
                break;
            case MeshMenuRow::NAME: {
                const char* nm = Squachy::customName();
                if (!nm) nm = Squachy::nickname();
                row(t, w, y, rowH, Theme::tr("NAME", "ИМЯ"), nm, Theme::VAPOR_YELLOW);
            } break;
#if MESH_COMPANION
            case MeshMenuRow::TARGET:
                row(t, w, y, rowH, Theme::tr("TARGET", "ЦЕЛЬ"), Settings::companionTargetLabel(), Theme::CYAN);
                break;
            case MeshMenuRow::NODE: {
                char b[24];
                if (MeshLink::connected()) snprintf(b, sizeof b, "%s", Theme::tr("LINKED >", "В СЕТИ >"));
                else if (MeshLink::nodeCount()) snprintf(b, sizeof b, "%u %s >", (unsigned)MeshLink::nodeCount(), Theme::tr("FOUND", "НАЙД"));
                else snprintf(b, sizeof b, "%s >", MeshLink::stateLabel());
                row(t, w, y, rowH, Theme::tr("NODE", "НОДА"), b,
                    MeshLink::connected() ? Theme::GREEN : (MeshLink::nodeCount() ? Theme::CYAN : Theme::W95_SHADOW));
            } break;
            case MeshMenuRow::CHANNELS: {
                char b[16];
                snprintf(b, sizeof b, "%u >", (unsigned)MeshLink::channelCount());
                row(t, w, y, rowH, Theme::tr("CHANNELS", "КАНАЛЫ"), b, MeshLink::channelCount() ? Theme::CYAN : Theme::W95_SHADOW);
            } break;
            case MeshMenuRow::CONTACTS: {
                char b[16];
                snprintf(b, sizeof b, "%u >", (unsigned)MeshLink::contactCount());
                row(t, w, y, rowH, Theme::tr("CONTACTS", "КОНТАКТЫ"), b, MeshLink::contactCount() ? Theme::CYAN : Theme::W95_SHADOW);
            } break;
            case MeshMenuRow::CHAT: {
                char b[16];
                const uint8_t inbox = MeshLink::inboxCount();
                snprintf(b, sizeof b, "%u >", (unsigned)inbox);
                row(t, w, y, rowH, Theme::tr("MESSAGES", "СООБЩ"), b, inbox ? Theme::GREEN : Theme::W95_SHADOW);
            } break;
#endif
            default: break;
        }
    }

    // The note line is BROMESH's; COMPANION gets a one-liner of its own.
    const bool below = top + n * rowH + 6 + 2 * t.fontHeight() + 3 <= h - Theme::pinnedBackH(t.width()) - 2;
    const char* note = nullptr;
    const char* mnote = nullptr;
#if MESH_COMPANION
    if (Settings::companionMode()) {
        note = Theme::tr("Companion: talk to a LoRa node.", "Компаньон: связь с LoRa-нодой.");
        if (MeshLink::state() == MeshLink::State::ERROR) mnote = MeshLink::stateLabel();
    } else
#endif
    {
        const bool d = Settings::meshDetect(), x = Settings::meshTransmit();
        note = d && x ? Theme::tr("You see them, they see you.", "Вы видите друг друга.")
             : d      ? Theme::tr("You see them. They cannot see you.", "Ты их видишь. Они тебя — нет.")
             : x      ? Theme::tr("They see you. You cannot see them.", "Они тебя видят. Ты их — нет.")
                      : Theme::tr("Off. Nothing sent, nothing shown.", "Выкл. Ничего не шлём, не кажем.");
        mnote = !MeshTalk::selfTestOk()      ? Theme::tr("Messages off: crypto self-test failed.", "Письма выкл: криптотест провален.")
              : !Settings::messagesOn()      ? nullptr
              : !MeshTalk::havePhrase()      ? Theme::tr("Messages need a phrase to share first.", "Письмам нужна общая фраза.")
              : !Settings::meshDetect()      ? Theme::tr("Messages need DETECT on to be heard.", "Чтоб слышать, включи СЛУШАТЬ.")
              : !Settings::meshTransmit()    ? Theme::tr("Messages: you can read, not reply.", "Письма: читаю, не отвечаю.")
                                             : Theme::tr("Messages: reading and replying.", "Письма: читаю и отвечаю.");
    }
    t.setTextSize(1);
    t.setTextColor(Theme::W95_LIGHT, Theme::BG);
    if (below && note) {
        t.setCursor(8, top + n * rowH + 6);
        Theme::printRU(t, note);
    }
    if (below && mnote) {
        t.setCursor(8, top + n * rowH + 6 + t.fontHeight() + 3);
        Theme::printRU(t, mnote);
    }
    if (!below) {
        const char* one = mnote ? mnote : note;
        if (one) {
            t.setTextColor(mnote ? Theme::AMBER : Theme::W95_LIGHT, Theme::BG);
            t.setCursor(w - 8 - Theme::textWidthRU(t, one), Theme::LIST_TOP + (Theme::LIST_HEADING_H - t.fontHeight()) / 2);
            Theme::printRU(t, one);
        }
    }
    Theme::drawPinnedBack(t, Theme::tr("[ BACK ]", "[ НАЗАД ]"));
}

MeshMenuRow uiMeshMenuHitTest(TFT_eSPI& t, int x, int y, int screenW, int screenH) {
    (void)screenW; (void)screenH;
    int top, rowH;
    geom(t, top, rowH);
    if (x < 0 || y < top) return MeshMenuRow::NONE;
    const int idx = (y - top) / rowH;
    MeshMenuRow rows[8];
    const uint8_t n = buildRows(rows);
    if (idx < 0 || idx >= (int)n) return MeshMenuRow::NONE;
    return rows[idx];
}

#endif // SQUACH_MESH
