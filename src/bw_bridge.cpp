// BroWatch — USB bridge implementation. See include/bw_bridge.h.
#include "bw_bridge.h"

#if defined(BW_BRIDGE) && defined(SQUACH_MESH) && SQUACH_MESH

#include <Arduino.h>
#include <esp_system.h>   // esp_random(), for the emote setup roll
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "detection.h"
#include "meshtalk.h"
#include "meshmsg.h"
#include "squachy.h"
#include "emote_script.h"
#include "settings.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif
#ifndef SQW_ENV
#define SQW_ENV "unknown"
#endif

namespace BwBridge {
namespace {

// Latest detection already forwarded, so repeats are not re-sent.
struct DetSig {
    bool     have = false;
    uint8_t  mac[6] = {0};
    uint8_t  type = 0;
    uint32_t firstSeen = 0;
};
// Latest emote already forwarded (peeked, never consumed: the CLEAR screen
// still owns takeEmote() and acts it out).
struct EmoteSig {
    bool     have = false;
    uint8_t  mac[6] = {0};
    uint32_t at = 0;
    uint8_t  emote = 0;
};

static uint32_t s_lastMsgAt   = 0;   // newest inbox millis() already sent
static uint32_t s_lastPeerMs  = 0;   // last squad snapshot
static uint32_t s_lastOwnMs   = 0;   // last own peer re-announce
static bool     s_announced   = false;
static DetSig   s_det;
static EmoteSig s_emote;

static const uint32_t PEER_EVERY_MS = 5000;
static const uint32_t OWN_EVERY_MS  = 30000;
static const uint8_t  MAX_MSG_PER_TICK = 4;

static void macStr(const uint8_t mac[6], char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// JSON string escaping into out (cap includes the NUL). Passes UTF-8 (>=0x80)
// through untouched; the gateway and the web app are UTF-8 end to end.
static void escJson(const char* src, char* out, size_t cap) {
    size_t o = 0;
    if (!src) src = "";
    for (const uint8_t* p = (const uint8_t*)src; *p && o + 1 < cap; p++) {
        const char* esc = nullptr;
        char tmp[7];
        if (*p == '"') esc = "\\\"";
        else if (*p == '\\') esc = "\\\\";
        else if (*p == '\n') esc = "\\n";
        else if (*p == '\r') esc = "\\r";
        else if (*p == '\t') esc = "\\t";
        else if (*p < 0x20) { snprintf(tmp, sizeof tmp, "\\u%04x", *p); esc = tmp; }
        if (esc) {
            for (const char* q = esc; *q && o + 1 < cap; q++) out[o++] = *q;
        } else {
            out[o++] = (char)*p;
        }
    }
    out[o] = '\0';
}

static void emit(const char* body) {
    Serial.print("[BW ");
    Serial.print(body);
    Serial.println("]");
}

// The board itself, as a peer the web list never loses: id is the BLE MAC it
// signs mesh frames with, name is short and stable across reboots.
static void emitOwn() {
    const uint8_t* mac = MeshTalk::ownMac();
    char macS[18], name[16], client[64], body[256];
    macStr(mac, macS);
    snprintf(name, sizeof name, "USB-%02X%02X", mac[4], mac[5]);
    snprintf(client, sizeof client, "bw %s/%s", SQW_ENV, FIRMWARE_VERSION);
    char lang = Settings::lang() == 1 ? 'R' : 'E';
    char nameE[32], clientE[96];
    escJson(name, nameE, sizeof nameE);
    escJson(client, clientE, sizeof clientE);
    snprintf(body, sizeof body,
             "{\"t\":\"peer\",\"mac\":\"%s\",\"name\":\"%s\",\"client\":\"%s\",\"lang\":\"%c\"}",
             macS, nameE, clientE, lang);
    emit(body);
}

static const char* peerName(const SquachMesh::Peer& p) {
    return (p.custom && p.name[0]) ? p.name : Squachy::nicknameAt(p.nick);
}

static void emitSnapshot(uint32_t now) {
    Mesh::SquadMember members[8];
    const uint8_t n = Mesh::squadList(now, members, 8);
    for (uint8_t i = 0; i < n; i++) {
        char macS[18], body[256], nameE[48];
        macStr(members[i].mac, macS);
        escJson(peerName(members[i].peer), nameE, sizeof nameE);
        snprintf(body, sizeof body,
                 "{\"t\":\"peer\",\"mac\":\"%s\",\"name\":\"%s\",\"client\":\"bw\"}",
                 macS, nameE);
        emit(body);
    }
    s_lastPeerMs = now;
}

// --- tiny JSON field reader (no heap, no library) ---------------------------
// Finds "key" then reads a string value (with \" \\ \n \r \t \u escapes) or a
// bare token (number/true/false) into out. False when absent.
static bool jfield(const char* js, const char* key, char* out, size_t cap) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char* p = strstr(js, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    size_t o = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"' && o + 1 < cap) {
            if (*p == '\\' && p[1]) {
                p++;
                char c = *p;
                if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
                else if (c == 'u') {  // \uXXXX -> '?', web text rarely needs more
                    c = '?';
                    for (int k = 0; k < 4 && p[1]; k++) p++;
                }
                out[o++] = c;
                p++;
            } else {
                out[o++] = *p++;
            }
        }
        out[o] = '\0';
        return true;
    }
    while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\t' && o + 1 < cap)
        out[o++] = *p++;
    out[o] = '\0';
    return o > 0;
}

}  // namespace

void begin() {
    s_lastMsgAt = 0;
    s_lastPeerMs = 0;
    s_lastOwnMs = 0;
    s_announced = false;
    s_det.have = false;
    s_emote.have = false;
}

void tick(uint32_t now, const DetectionEngine& eng) {
    // Our MAC arrives with the radio; announce on first sight, then monthly.
    const uint8_t* mac = MeshTalk::ownMac();
    bool macOk = false;
    for (uint8_t i = 0; i < 6; i++) macOk |= (mac[i] != 0);
    if (macOk && (!s_announced || now - s_lastOwnMs > OWN_EVERY_MS)) {
        emitOwn();
        s_announced = true;
        s_lastOwnMs = now;
    }
    if (!macOk) return;

    // Inbox, oldest first (index 0 is the newest). Marks by arrival millis so
    // a reboot backlog older than INBOX_N is quietly skipped, not flooded.
    const uint8_t n = MeshTalk::inboxCount();
    uint32_t maxAt = s_lastMsgAt;
    uint8_t sent = 0;
    for (int i = n - 1; i >= 0 && sent < MAX_MSG_PER_TICK; i--) {
        const MeshTalk::Message& m = MeshTalk::inboxAt((uint8_t)i);
        if (!m.have) continue;
        if (m.at > maxAt) maxAt = m.at;
        if (m.at <= s_lastMsgAt) continue;
        const char* text = MeshTalk::lineText(m);
        char fromE[32], textE[160], body[256];
        escJson(m.from, fromE, sizeof fromE);
        escJson(text ? text : "", textE, sizeof textE);
        snprintf(body, sizeof body, "{\"t\":\"msg\",\"from\":\"%s\",\"text\":\"%s\"}",
                 fromE, textE);
        emit(body);
        sent++;
    }
    s_lastMsgAt = maxAt;

    // Latest detection, once per sighting (MAC + type + first-seen session).
    const Detection* d = eng.latest();
    if (d) {
        bool same = s_det.have && s_det.type == (uint8_t)d->type &&
                    s_det.firstSeen == d->firstSeen &&
                    memcmp(s_det.mac, d->mac, 6) == 0;
        if (!same) {
            char macS[18], vendorE[48], body[256];
            macStr(d->mac, macS);
            escJson(vendorText(*d), vendorE, sizeof vendorE);
            snprintf(body, sizeof body,
                     "{\"t\":\"detection\",\"type\":\"%s\",\"mac\":\"%s\",\"rssi\":%d,\"vendor\":\"%s\"}",
                     detectionTypeName(d->type), macS, (int)d->rssi, vendorE);
            emit(body);
            memcpy(s_det.mac, d->mac, 6);
            s_det.type = (uint8_t)d->type;
            s_det.firstSeen = d->firstSeen;
            s_det.have = true;
        }
    }

    // Emotes: peeked, never consumed. The CLEAR screen still owns takeEmote().
    MeshTalk::EmoteIn e;
    if (MeshTalk::peekEmote(e)) {
        bool same = s_emote.have && s_emote.at == e.at && s_emote.emote == e.emote &&
                    memcmp(s_emote.mac, e.mac, 6) == 0;
        if (!same) {
            char macS[18], body[192];
            macStr(e.mac, macS);
            const char* nm = "";
            if (e.emote < (uint8_t)MeshMsg::Emote::COUNT)
                nm = EmoteScript::name((MeshMsg::Emote)e.emote);
            char nmE[32];
            escJson(nm, nmE, sizeof nmE);
            snprintf(body, sizeof body, "{\"t\":\"emote\",\"from\":\"%s\",\"emote\":\"%s\"}",
                     macS, nmE);
            emit(body);
            memcpy(s_emote.mac, e.mac, 6);
            s_emote.at = e.at;
            s_emote.emote = e.emote;
            s_emote.have = true;
        }
    }

    if (now - s_lastPeerMs > PEER_EVERY_MS) emitSnapshot(now);
}

void onLine(const char* line) {
    if (!line || strncmp(line, "[BW", 3) != 0) return;
    const char* js = strchr(line, '{');
    if (!js) return;
    char t[16];
    if (!jfield(js, "t", t, sizeof t)) return;
    const uint32_t now = millis();

    if (strcasecmp(t, "send") == 0) {
        char text[MeshMsg::TEXT_MAX + 1];
        if (jfield(js, "text", text, sizeof text) && text[0]) {
            MeshTalk::sendText(text, now);
            return;
        }
        char num[12];
        if (jfield(js, "canned", num, sizeof num)) {
            MeshTalk::send((uint8_t)atoi(num), now);
            return;
        }
    } else if (strcasecmp(t, "emote") == 0) {
        char num[12];
        if (jfield(js, "emote", num, sizeof num)) {
            const int e = atoi(num);
            if (e >= 0 && e < (int)MeshMsg::Emote::COUNT) {
                const uint8_t setup = EmoteScript::roll(
                    (MeshMsg::Emote)e, esp_random(), (uint8_t)Squachy::lastCaught());
                MeshTalk::sendEmote((uint8_t)e, setup, now);
            }
        }
    } else if (strcasecmp(t, "hello") == 0) {
        // A listener just came up (gateway start, page reload): hand it the
        // board as a peer plus everything current, so the web UI fills in.
        emitOwn();
        s_lastOwnMs = now;
        s_announced = true;
        s_lastMsgAt = 0;   // resend the inbox backlog below on this pass
        s_det.have = false;
        s_emote.have = false;
        emitSnapshot(now);
    } else if (strcasecmp(t, "ping") == 0) {
        // Lightweight keepalive (gateway sends one every ~25 s): announce +
        // snapshot only. Unlike hello this MUST NOT rewind the inbox /
        // detection / emote cursors — otherwise every ping re-floods the web
        // chat with the whole backlog as duplicates.
        emitOwn();
        s_lastOwnMs = now;
        s_announced = true;
        emitSnapshot(now);
    }
}

}  // namespace BwBridge

#else   // !BW_BRIDGE or no mesh: link-safe stubs so call sites need no guards

#include <stdint.h>
class DetectionEngine;
namespace BwBridge {
void begin() {}
void tick(uint32_t, const DetectionEngine&) {}
void onLine(const char*) {}
}  // namespace BwBridge

#endif
