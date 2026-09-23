// SquachWatch-CYD — the companion screens. See include/ui_meshlink.h.
#include "ui_meshlink.h"

#if MESH_COMPANION

#include "mesh_link.h"
#include "settings.h"
#include "theme.h"
#include "ui_scroll.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

const int BTN_H  = 28;
const int ROW_Y0 = 36;
const int ROW_H  = 26;
const int ROW_GAP = 3;
const int ROW_PITCH = ROW_H + ROW_GAP;

int s_nodeSel = -1;

// Scroll offsets, in rows, one per list. The lists can run past the screen
// (25 contacts, a long conversation), so every one of them scrolls and draws
// a bar; the touch gesture that drives them lives in main.cpp, the same
// press-slide-release shape the log screen uses.
int s_nodesScroll    = 0;
int s_chansScroll    = 0;
int s_contactsScroll = 0;
int s_chatScroll     = 0;

bool in(int x, int y, int bx, int by, int bw, int bh) {
    const int slop = 6;
    return x >= bx - slop && x < bx + bw + slop && y >= by - slop && y < by + bh + slop;
}

struct Bar { int rows, w, x[4], y[4]; };
Bar bar(TFT_eSPI& t, int n) {
    Bar b;
    const int gap = 8;
    const bool wide = t.width() >= 300;
    const int perRow = wide ? n : 2;
    b.rows = (n + perRow - 1) / perRow;
    b.w = (t.width() - 16 - (perRow - 1) * gap) / perRow;
    if (b.w > 110) b.w = 110;
    for (int i = 0; i < n; i++) {
        const int r = i / perRow, c = i % perRow;
        const int span = perRow * b.w + (perRow - 1) * gap;
        b.x[i] = (t.width() - span) / 2 + c * (b.w + gap);
        b.y[i] = t.height() - 6 - (b.rows - r) * BTN_H - (b.rows - 1 - r) * 6;
    }
    return b;
}

int rowsThatFit(TFT_eSPI& t, int barTop) {
    const int n = (barTop - 6 - ROW_Y0) / (ROW_H + ROW_GAP);
    return n < 1 ? 1 : n;
}

void statusLine(TFT_eSPI& t, uint32_t now) {
    t.setTextSize(1);
    t.setTextColor(MeshLink::connected() ? Theme::GREEN : Theme::CYAN, Theme::BG);
    t.setCursor(8, 22);
    char b[48];
    snprintf(b, sizeof b, "%s  %s", Settings::companionTargetLabel(), MeshLink::stateLabel());
    Theme::printRU(t, b);
    (void)now;
}

// A conversation is either the active channel or one contact.
const char* chatTitle(TFT_eSPI& t, char* buf, size_t cap) {
    if (MeshLink::dmTarget()) {
        const char* nm = nullptr;
        for (uint8_t i = 0; i < MeshLink::contactCount(); i++)
            if (MeshLink::contactAt(i).num == MeshLink::dmTarget()) {
                const MeshLink::Contact& c = MeshLink::contactAt(i);
                nm = c.shortName[0] ? c.shortName : c.longName;
            }
        snprintf(buf, cap, "DM %s", nm ? nm : "?");
    } else {
        snprintf(buf, cap, "%s", MeshLink::channelAt(MeshLink::sendChannel()).name);
    }
    (void)t;
    return buf;
}

bool msgMatches(const MeshLink::Message& m) {
    if (MeshLink::dmTarget())
        return m.direct && (m.fromNum == MeshLink::dmTarget() || m.toNum == MeshLink::dmTarget());
    return !m.direct && m.channel == MeshLink::sendChannel();
}

// Copy at most `maxBytes` bytes, never splitting a UTF-8 character (a Russian
// letter is two), and always NUL-terminate. The old "%.44s" cut a Russian
// message mid-letter.
void copyUtf8(char* dst, size_t cap, const char* src, size_t maxBytes) {
    size_t n = src ? strlen(src) : 0;
    if (n > maxBytes) {
        n = maxBytes;
        while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
    }
    if (n > cap - 1) n = cap - 1;
    if (n && src) memcpy(dst, src, n);
    dst[n] = '\0';
}

} // namespace

// Positive scrolls down (toward older/later rows). The gesture that calls
// these lives in main.cpp; each list clamps itself when it draws.
void uiMeshNodesScroll(int delta)    { s_nodesScroll += delta; }
void uiMeshChannelsScroll(int delta) { s_chansScroll += delta; }
void uiMeshContactsScroll(int delta) { s_contactsScroll += delta; }
void uiMeshChatScroll(int delta)     { s_chatScroll += delta; }

// =====================================================================
//  NODES
// =====================================================================
void uiMeshNodesInit(TFT_eSPI& t) {
    s_nodeSel = -1;
    s_nodesScroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}
void uiMeshNodesSelect(int row)   { s_nodeSel = row; }
int  uiMeshNodesSelected()        { return s_nodeSel; }

void uiMeshNodesTick(TFT_eSPI& t, uint32_t now, bool advance) {
    (void)advance;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> NODES <<");
    statusLine(t, now);
    t.setTextSize(1);
    t.setTextWrap(false);
    const int w = t.width();
    const Bar b = bar(t, 4);
    const uint8_t n = MeshLink::nodeCount();
    if (!n) {
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(8, ROW_Y0 + 4);
        Theme::printRU(t, Theme::tr("No nodes yet. SCAN while the radio node is on and nearby.",
                                    "Нод нет. ЖМИ СКАН, когда радионода рядом включена."));
    }
    const int fit = rowsThatFit(t, b.y[0]);
    const int bodyH = b.y[0] - ROW_Y0 - 6;
    uiClampScroll(s_nodesScroll, n, bodyH, ROW_PITCH);
    for (int vi = 0; vi < fit; vi++) {
        const int i = s_nodesScroll + vi;
        if (i >= (int)n) break;
        const MeshLink::Node& nd = MeshLink::nodeAt((uint8_t)i);
        const int y = ROW_Y0 + vi * ROW_PITCH;
        const bool sel = i == s_nodeSel;
        t.fillRect(8, y, w - 16, ROW_H, Theme::TASKBAR);
        t.drawRect(8, y, w - 16, ROW_H, sel ? Theme::VAPOR_PINK : Theme::VAPOR_PURPLE);
        char mac[20];
        snprintf(mac, sizeof mac, "%02X:%02X:%02X:%02X:%02X:%02X",
                 nd.mac[0], nd.mac[1], nd.mac[2], nd.mac[3], nd.mac[4], nd.mac[5]);
        t.setTextColor(nd.name[0] ? Theme::WHITE : Theme::W95_SHADOW, Theme::TASKBAR);
        t.setCursor(14, y + 4);
        Theme::printRU(t, nd.name[0] ? nd.name : "(unnamed)");
        t.setTextColor(Theme::W95_LIGHT, Theme::TASKBAR);
        t.setCursor(14, y + 15);
        Theme::printRU(t, mac);
        char db[12];
        snprintf(db, sizeof db, "%d dBm", nd.rssi);
        t.setTextColor(Theme::CYAN, Theme::TASKBAR);
        t.setCursor(w - 16 - Theme::textWidthRU(t, db), y + 15);
        Theme::printRU(t, db);
    }
    if (n > (uint8_t)fit) Theme::drawScrollbar(t, w - 4, ROW_Y0, bodyH, n, fit, s_nodesScroll);
    const bool haveSel = s_nodeSel >= 0 && s_nodeSel < n;
    Theme::drawWin95Button(t, b.x[0], b.y[0], b.w, BTN_H, Theme::tr("SCAN", "СКАН"), false);
    Theme::drawWin95Button(t, b.x[1], b.y[1], b.w, BTN_H, Theme::tr("CONNECT", "СВЯЗАТЬ"), !haveSel);
    Theme::drawWin95Button(t, b.x[2], b.y[2], b.w, BTN_H, Theme::tr("DROP", "ОТКЛ"), !MeshLink::connected());
    Theme::drawWin95Button(t, b.x[3], b.y[3], b.w, BTN_H, Theme::tr("BACK", "НАЗАД"), false);
    Theme::drawToast(t, now);
}

MeshNodesHit uiMeshNodesHit(TFT_eSPI& t, int x, int y, int* row) {
    const Bar b = bar(t, 4);
    const int fit = rowsThatFit(t, b.y[0]);
    const uint8_t n = MeshLink::nodeCount();
    for (int vi = 0; vi < fit; vi++) {
        const int i = s_nodesScroll + vi;
        if (i >= (int)n) break;
        const int ry = ROW_Y0 + vi * ROW_PITCH;
        if (y >= ry && y < ry + ROW_PITCH && x >= 8 && x <= t.width() - 8) {
            if (row) *row = i;
            return MeshNodesHit::ROW;
        }
    }
    if (in(x, y, b.x[0], b.y[0], b.w, BTN_H)) return MeshNodesHit::SCAN;
    if (in(x, y, b.x[1], b.y[1], b.w, BTN_H)) return MeshNodesHit::CONNECT;
    if (in(x, y, b.x[2], b.y[2], b.w, BTN_H)) return MeshNodesHit::DISCONNECT;
    if (in(x, y, b.x[3], b.y[3], b.w, BTN_H)) return MeshNodesHit::BACK;
    return MeshNodesHit::NONE;
}

// =====================================================================
//  CHANNELS
// =====================================================================
void uiMeshChannelsInit(TFT_eSPI& t) {
    s_chansScroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshChannelsTick(TFT_eSPI& t, uint32_t now, bool advance) {
    (void)advance;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> CHANNELS <<");
    statusLine(t, now);
    t.setTextSize(1);
    t.setTextWrap(false);
    const int w = t.width();
    const Bar b = bar(t, 1);
    const uint8_t n = MeshLink::channelCount();
    const int fit = rowsThatFit(t, b.y[0]);
    const int bodyH = b.y[0] - ROW_Y0 - 6;
    uiClampScroll(s_chansScroll, n, bodyH, ROW_PITCH);
    for (int vi = 0; vi < fit; vi++) {
        const int i = s_chansScroll + vi;
        if (i >= (int)n) break;
        const MeshLink::Channel& c = MeshLink::channelAt((uint8_t)i);
        const int y = ROW_Y0 + vi * ROW_PITCH;
        const bool active = !MeshLink::dmTarget() && c.index == MeshLink::sendChannel();
        const bool off = c.role == 0;
        const char* roleTxt = c.role == 1 ? "PRIMARY" : c.role == 2 ? "SECOND" : "OFF";
        t.fillRect(8, y, w - 16, ROW_H, Theme::TASKBAR);
        t.drawRect(8, y, w - 16, ROW_H, active ? Theme::GREEN : (off ? Theme::W95_SHADOW : Theme::VAPOR_PURPLE));
        char head[32];
        snprintf(head, sizeof head, "%u  %s", (unsigned)c.index, c.name);
        t.setTextColor(off ? Theme::W95_SHADOW : Theme::WHITE, Theme::TASKBAR);
        t.setCursor(14, y + 4);
        Theme::printRU(t, head);
        t.setTextColor(c.hasPsk ? Theme::CYAN : Theme::W95_LIGHT, Theme::TASKBAR);
        t.setCursor(14, y + 15);
        Theme::printRU(t, c.hasPsk ? Theme::tr("keyed", "с ключом") : Theme::tr("open", "без ключа"));
        t.setTextColor(off ? Theme::W95_SHADOW : Theme::VAPOR_YELLOW, Theme::TASKBAR);
        t.setCursor(w - 16 - Theme::textWidthRU(t, roleTxt), y + 4);
        Theme::printRU(t, roleTxt);
        if (active) {
            t.setTextColor(Theme::GREEN, Theme::TASKBAR);
            t.setCursor(w - 16 - Theme::textWidthRU(t, "<-"), y + 15);
            Theme::printRU(t, "<-");
        }
    }
    if (n > (uint8_t)fit) Theme::drawScrollbar(t, w - 4, ROW_Y0, bodyH, n, fit, s_chansScroll);
    Theme::drawWin95Button(t, b.x[0], b.y[0], b.w, BTN_H, Theme::tr("BACK", "НАЗАД"), false);
    Theme::drawToast(t, now);
}

MeshChannelsHit uiMeshChannelsHit(TFT_eSPI& t, int x, int y, int* row) {
    const Bar b = bar(t, 1);
    const int fit = rowsThatFit(t, b.y[0]);
    const uint8_t n = MeshLink::channelCount();
    for (int vi = 0; vi < fit; vi++) {
        const int i = s_chansScroll + vi;
        if (i >= (int)n) break;
        const int ry = ROW_Y0 + vi * ROW_PITCH;
        if (y >= ry && y < ry + ROW_PITCH && x >= 8 && x <= t.width() - 8) {
            if (row) *row = i;
            return MeshChannelsHit::ROW;
        }
    }
    if (in(x, y, b.x[0], b.y[0], b.w, BTN_H)) return MeshChannelsHit::BACK;
    return MeshChannelsHit::NONE;
}

// =====================================================================
//  CONTACTS
// =====================================================================
void uiMeshContactsInit(TFT_eSPI& t) {
    s_contactsScroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshContactsTick(TFT_eSPI& t, uint32_t now, bool advance) {
    (void)advance;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> CONTACTS <<");
    statusLine(t, now);
    t.setTextSize(1);
    t.setTextWrap(false);
    const int w = t.width();
    const Bar b = bar(t, 1);
    const uint8_t n = MeshLink::contactCount();
    const int fit = rowsThatFit(t, b.y[0]);
    const int bodyH = b.y[0] - ROW_Y0 - 6;
    uiClampScroll(s_contactsScroll, n, bodyH, ROW_PITCH);
    if (!n) {
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(8, ROW_Y0 + 4);
        Theme::printRU(t, Theme::tr("No contacts yet. They arrive once the node shares its list.",
                                    "Контактов нет. Нода пришлёт свой список."));
    }
    for (int vi = 0; vi < fit; vi++) {
        const int i = s_contactsScroll + vi;
        if (i >= (int)n) break;
        const MeshLink::Contact& c = MeshLink::contactAt((uint8_t)i);
        const int y = ROW_Y0 + vi * ROW_PITCH;
        const bool sel = MeshLink::dmTarget() == c.num;
        t.fillRect(8, y, w - 16, ROW_H, Theme::TASKBAR);
        t.drawRect(8, y, w - 16, ROW_H, sel ? Theme::GREEN : Theme::VAPOR_PURPLE);
        char head[40];
        snprintf(head, sizeof head, "%s", c.shortName[0] ? c.shortName : (c.longName[0] ? c.longName : "?"));
        t.setTextColor(Theme::WHITE, Theme::TASKBAR);
        t.setCursor(14, y + 4);
        Theme::printRU(t, head);
        char sub[40];
        snprintf(sub, sizeof sub, "%s %s", c.longName, c.hasKey ? Theme::tr("[key]", "[ключ]") : "");
        t.setTextColor(c.hasKey ? Theme::CYAN : Theme::W95_LIGHT, Theme::TASKBAR);
        t.setCursor(14, y + 15);
        Theme::printRU(t, sub);
        char num[12];
        snprintf(num, sizeof num, "%08X", (unsigned)c.num);
        t.setTextColor(Theme::VAPOR_YELLOW, Theme::TASKBAR);
        t.setCursor(w - 16 - Theme::textWidthRU(t, num), y + 15);
        Theme::printRU(t, num);
    }
    if (n > (uint8_t)fit) Theme::drawScrollbar(t, w - 4, ROW_Y0, bodyH, n, fit, s_contactsScroll);
    Theme::drawWin95Button(t, b.x[0], b.y[0], b.w, BTN_H, Theme::tr("BACK", "НАЗАД"), false);
    Theme::drawToast(t, now);
}

MeshContactsHit uiMeshContactsHit(TFT_eSPI& t, int x, int y, int* row) {
    const Bar b = bar(t, 1);
    const int fit = rowsThatFit(t, b.y[0]);
    const uint8_t n = MeshLink::contactCount();
    for (int vi = 0; vi < fit; vi++) {
        const int i = s_contactsScroll + vi;
        if (i >= (int)n) break;
        const int ry = ROW_Y0 + vi * ROW_PITCH;
        if (y >= ry && y < ry + ROW_PITCH && x >= 8 && x <= t.width() - 8) {
            if (row) *row = i;
            return MeshContactsHit::ROW;
        }
    }
    if (in(x, y, b.x[0], b.y[0], b.w, BTN_H)) return MeshContactsHit::BACK;
    return MeshContactsHit::NONE;
}

// =====================================================================
//  CHAT (one conversation)
// =====================================================================
void uiMeshChatInit(TFT_eSPI& t) {
    s_chatScroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshChatTick(TFT_eSPI& t, uint32_t now, bool advance) {
    (void)advance;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    char tt[32];
    chatTitle(t, tt, sizeof tt);
    Theme::drawTitleBar(t, tt);
    statusLine(t, now);
    t.setTextSize(1);
    t.setTextWrap(false);
    const int w = t.width();
    const Bar b = bar(t, 3);
    const int fit = rowsThatFit(t, b.y[0]);
    const int bodyH = b.y[0] - ROW_Y0 - 6;
    const uint8_t n = MeshLink::inboxCount();
    int matchN = 0;
    for (int i = 0; i < (int)n; i++) if (msgMatches(MeshLink::inboxAt((uint8_t)i))) matchN++;
    uiClampScroll(s_chatScroll, matchN, bodyH, ROW_PITCH);
    int mi = 0;
    for (int i = 0; i < (int)n && mi < matchN; i++) {
        const MeshLink::Message& m = MeshLink::inboxAt((uint8_t)i);
        if (!msgMatches(m)) continue;
        const int vis = mi - s_chatScroll;
        mi++;
        if (vis < 0 || vis >= fit) continue;
        const int y = ROW_Y0 + vis * ROW_PITCH;
        t.fillRect(8, y, w - 16, ROW_H, Theme::TASKBAR);
        t.drawRect(8, y, w - 16, ROW_H, m.outgoing ? Theme::VAPOR_PURPLE : Theme::VAPOR_PINK);
        t.setTextColor(m.outgoing ? Theme::CYAN : Theme::VAPOR_YELLOW, Theme::TASKBAR);
        t.setCursor(14, y + 4);
        Theme::printRU(t, m.from);
        char body[48];
        copyUtf8(body, sizeof body, m.body, 44);
        t.setTextColor(Theme::WHITE, Theme::TASKBAR);
        t.setCursor(14, y + 15);
        Theme::printRU(t, body);
    }
    if (!matchN) {
        t.setTextColor(Theme::W95_SHADOW, Theme::BG);
        t.setCursor(8, ROW_Y0 + 4);
        Theme::printRU(t, Theme::tr("Nothing here yet.", "Пока пусто."));
    }
    if (matchN > fit) Theme::drawScrollbar(t, w - 4, ROW_Y0, bodyH, matchN, fit, s_chatScroll);
    static char chl[24];
    if (MeshLink::dmTarget()) snprintf(chl, sizeof chl, "%s", Theme::tr("CHANNELS", "КАНАЛЫ"));
    else                     snprintf(chl, sizeof chl, "%s", MeshLink::channelAt(MeshLink::sendChannel()).name);
    Theme::drawWin95Button(t, b.x[0], b.y[0], b.w, BTN_H, chl, false);
    Theme::drawWin95Button(t, b.x[1], b.y[1], b.w, BTN_H, Theme::tr("SEND", "ОТПР"), !MeshLink::connected());
    Theme::drawWin95Button(t, b.x[2], b.y[2], b.w, BTN_H, Theme::tr("BACK", "НАЗАД"), false);
    Theme::drawToast(t, now);
}

MeshChatHit uiMeshChatHit(TFT_eSPI& t, int x, int y) {
    const Bar b = bar(t, 3);
    if (in(x, y, b.x[0], b.y[0], b.w, BTN_H)) return MeshChatHit::CHANNEL;
    if (in(x, y, b.x[1], b.y[1], b.w, BTN_H)) return MeshChatHit::WRITE;
    if (in(x, y, b.x[2], b.y[2], b.w, BTN_H)) return MeshChatHit::BACK;
    return MeshChatHit::NONE;
}

// =====================================================================
//  SEND chooser (templates + MANUAL)
// =====================================================================
namespace {
constexpr uint8_t TPL_N = 6;
const char* tplText(uint8_t i) {
    switch (i) {
        case 0: return Theme::tr("Hello!", "Привет!");
        case 1: return Theme::tr("On the air.", "На связи.");
        case 2: return Theme::tr("How copy?", "Как слышно?");
        case 3: return Theme::tr("OK.", "Ок.");
        case 4: return Theme::tr("On my way.", "Скоро буду.");
        default:return Theme::tr("Need help?", "Нужна помощь?");
    }
}
int s_tplScroll = 0;
} // namespace

uint8_t     uiMeshTemplateCount() { return (uint8_t)(TPL_N + 1); }
const char* uiMeshTemplateAt(uint8_t i) {
    if (i == 0) return Theme::tr("MANUAL...", "ВРУЧНУЮ...");
    return (i <= TPL_N) ? tplText((uint8_t)(i - 1)) : "";
}
void uiMeshTemplatesScroll(int d) { s_tplScroll += d; }

void uiMeshTemplatesInit(TFT_eSPI& t) {
    s_tplScroll = 0;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshTemplatesTick(TFT_eSPI& t, uint32_t now, bool advance) {
    (void)advance;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> SEND <<");
    statusLine(t, now);
    t.setTextSize(1);
    t.setTextWrap(false);
    const int w = t.width();
    const Bar b = bar(t, 1);
    const uint8_t n = uiMeshTemplateCount();
    const int fit = rowsThatFit(t, b.y[0]);
    const int bodyH = b.y[0] - ROW_Y0 - 6;
    uiClampScroll(s_tplScroll, n, bodyH, ROW_PITCH);
    for (int vi = 0; vi < fit; vi++) {
        const int i = s_tplScroll + vi;
        if (i >= (int)n) break;
        const int y = ROW_Y0 + vi * ROW_PITCH;
        const bool manual = (i == 0);
        t.fillRect(8, y, w - 16, ROW_H, Theme::TASKBAR);
        t.drawRect(8, y, w - 16, ROW_H, manual ? Theme::GREEN : Theme::VAPOR_PURPLE);
        t.setTextColor(manual ? Theme::GREEN : Theme::WHITE, Theme::TASKBAR);
        t.setCursor(14, y + (ROW_H - t.fontHeight()) / 2);
        Theme::printRU(t, uiMeshTemplateAt((uint8_t)i));
    }
    if (n > (uint8_t)fit) Theme::drawScrollbar(t, w - 4, ROW_Y0, bodyH, n, fit, s_tplScroll);
    Theme::drawWin95Button(t, b.x[0], b.y[0], b.w, BTN_H, Theme::tr("BACK", "НАЗАД"), false);
    Theme::drawToast(t, now);
}

MeshTplHit uiMeshTemplatesHit(TFT_eSPI& t, int x, int y, int* row) {
    const Bar b = bar(t, 1);
    const int fit = rowsThatFit(t, b.y[0]);
    const uint8_t n = uiMeshTemplateCount();
    for (int vi = 0; vi < fit; vi++) {
        const int i = s_tplScroll + vi;
        if (i >= (int)n) break;
        const int ry = ROW_Y0 + vi * ROW_PITCH;
        if (y >= ry && y < ry + ROW_PITCH && x >= 8 && x <= t.width() - 8) {
            if (row) *row = i;
            return MeshTplHit::ROW;
        }
    }
    if (in(x, y, b.x[0], b.y[0], b.w, BTN_H)) return MeshTplHit::BACK;
    return MeshTplHit::NONE;
}

#endif // MESH_COMPANION