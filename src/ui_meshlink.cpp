// SquachWatch-CYD — the companion screens. See include/ui_meshlink.h.
#include "ui_meshlink.h"

#if MESH_COMPANION

#include "mesh_link.h"
#include "settings.h"
#include "theme.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

const int BTN_H  = 28;
const int ROW_Y0 = 36;
const int ROW_H  = 26;
const int ROW_GAP = 3;

int s_nodeSel = -1;

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

// A status line under the title: what the link is doing right now.
void statusLine(TFT_eSPI& t, uint32_t now) {
    t.setTextSize(1);
    t.setTextColor(MeshLink::connected() ? Theme::GREEN : Theme::CYAN, Theme::BG);
    t.setCursor(8, 22);
    char b[48];
    snprintf(b, sizeof b, "%s  %s", Settings::companionTargetLabel(), MeshLink::stateLabel());
    t.print(b);
    (void)now;
}

} // namespace

// =====================================================================
//  NODES
// =====================================================================
void uiMeshNodesInit(TFT_eSPI& t) {
    s_nodeSel = -1;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshNodesTick(TFT_eSPI& t, uint32_t now, bool advance) {
    (void)advance;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> COMPANION <<");
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
    for (int i = 0; i < n && i < fit; i++) {
        const MeshLink::Node& nd = MeshLink::nodeAt((uint8_t)i);
        const int y = ROW_Y0 + i * (ROW_H + ROW_GAP);
        const bool sel = i == s_nodeSel;
        t.fillRect(8, y, w - 16, ROW_H, Theme::TASKBAR);
        t.drawRect(8, y, w - 16, ROW_H, sel ? Theme::VAPOR_PINK : Theme::VAPOR_PURPLE);
        char mac[20];
        snprintf(mac, sizeof mac, "%02X:%02X:%02X:%02X:%02X:%02X",
                 nd.mac[0], nd.mac[1], nd.mac[2], nd.mac[3], nd.mac[4], nd.mac[5]);
        t.setTextColor(nd.name[0] ? Theme::WHITE : Theme::W95_SHADOW, Theme::TASKBAR);
        t.setCursor(14, y + 4);
        t.print(nd.name[0] ? nd.name : "(unnamed)");
        t.setTextColor(Theme::W95_LIGHT, Theme::TASKBAR);
        t.setCursor(14, y + 15);
        t.print(mac);
        char db[12];
        snprintf(db, sizeof db, "%d dBm", nd.rssi);
        t.setTextColor(Theme::CYAN, Theme::TASKBAR);
        t.setCursor(w - 16 - t.textWidth(db), y + 15);
        t.print(db);
    }
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
    for (int i = 0; i < n && i < fit; i++) {
        const int ry = ROW_Y0 + i * (ROW_H + ROW_GAP);
        if (y >= ry && y < ry + ROW_H + ROW_GAP && x >= 8 && x <= t.width() - 8) {
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
//  CHAT
// =====================================================================
void uiMeshChatInit(TFT_eSPI& t) {
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

void uiMeshChatTick(TFT_eSPI& t, uint32_t now, bool advance) {
    (void)advance;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
    Theme::drawTitleBar(t, ">> MESSAGES <<");
    statusLine(t, now);
    t.setTextSize(1);
    t.setTextWrap(false);
    const int w = t.width();
    const Bar b = bar(t, 3);
    const int fit = rowsThatFit(t, b.y[0]);
    const uint8_t n = MeshLink::inboxCount();
    if (!n) {
        t.setTextColor(Theme::WHITE, Theme::BG);
        t.setCursor(8, ROW_Y0 + 4);
        Theme::printRU(t, Theme::tr("Nothing yet.", "Пока пусто."));
    }
    for (int i = 0; i < n && i < fit; i++) {
        const MeshLink::Message& m = MeshLink::inboxAt((uint8_t)i);
        const int y = ROW_Y0 + i * (ROW_H + ROW_GAP);
        t.fillRect(8, y, w - 16, ROW_H, Theme::TASKBAR);
        t.drawRect(8, y, w - 16, ROW_H, m.outgoing ? Theme::VAPOR_PURPLE : Theme::VAPOR_PINK);
        char head[40];
        snprintf(head, sizeof head, "%s  ch%u", m.from, (unsigned)m.channel);
        t.setTextColor(m.outgoing ? Theme::CYAN : Theme::VAPOR_YELLOW, Theme::TASKBAR);
        t.setCursor(14, y + 4);
        t.print(head);
        char body[48];
        snprintf(body, sizeof body, "%.44s", m.body);
        t.setTextColor(Theme::WHITE, Theme::TASKBAR);
        t.setCursor(14, y + 15);
        t.print(body);
    }
    // Which channel a reply goes to.
    static char chl[24];
    if (MeshLink::channelCount())
        snprintf(chl, sizeof chl, "%s", MeshLink::channelAt(MeshLink::sendChannel()).name);
    else
        snprintf(chl, sizeof chl, "CH %u", (unsigned)MeshLink::sendChannel());
    Theme::drawWin95Button(t, b.x[0], b.y[0], b.w, BTN_H, chl, false);
    Theme::drawWin95Button(t, b.x[1], b.y[1], b.w, BTN_H, Theme::tr("WRITE", "ПИСАТЬ"), !MeshLink::connected());
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

#endif // MESH_COMPANION