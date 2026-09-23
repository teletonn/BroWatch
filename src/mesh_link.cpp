// SquachWatch-CYD — the companion link. See include/mesh_link.h for what this
// is and for the threading contract.
#include "mesh_link.h"

#if MESH_COMPANION

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_system.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

namespace MeshLink {
namespace {

// -------- the node's own UUIDs (Meshtastic BluetoothCommon.h) --------
const char* kMeshSvc  = "6ba1b218-15a8-461f-9fa8-5dcae273eafd";
const char* kToRadio  = "f75c76d2-129e-4dad-a1dd-7866124401e7";
const char* kFromRadio= "2c55e69e-4993-11ed-b878-0242ac120002";
const char* kFromNum  = "ed9da18c-a800-4f66-a670-aa7547e34453";
#if MESH_COMPANION_AUTOSTART
const char* kLogRadio = "5a3d6e49-06e6-4423-9944-e9de8cdf9547";
#endif

constexpr uint32_t WANT_CONFIG_ID = 0x50C0FFEE;
constexpr uint32_t BROADCAST_TO   = 0xFFFFFFFFu;

// -------- shared state (volatile: written by the task, read by the screens) --
volatile State    s_state    = State::OFF;
volatile Target   s_target   = Target::MESHTASTIC;
volatile bool     s_scanning = false;
volatile uint8_t  s_sendChan = 0;
volatile uint32_t s_stateAt  = 0;
char              s_reason[32] = "";      // why ERROR, for stateLabel()
volatile uint8_t  s_ownMac[6]  = {0,0,0,0,0,0};

// -------- discovery list (host task writes, loop task reads) --------
SemaphoreHandle_t s_nodeMux = nullptr;
Node     s_nodes[NODE_MAX];
uint8_t  s_nodeN = 0;

// -------- channels --------
Channel  s_chans[CHAN_MAX];
uint8_t  s_chanN = 0;
// Show this channel's mail on the main screen (toast + herald bubble). DMs
// always notify; channels are opt-in per channel, primary on by default.
bool     s_notify[CHAN_MAX] = {false};

// -------- contacts: every node the radio has told us about --------
Contact  s_contacts[CONTACT_MAX];
uint8_t  s_contactN = 0;
uint32_t s_dmTarget = 0;

static Contact* findContact(uint32_t num) {
    for (uint8_t i = 0; i < s_contactN; i++)
        if (s_contacts[i].num == num) return &s_contacts[i];
    return nullptr;
}
static Contact* upsertContact(uint32_t num) {
    if (!num) return nullptr;
    Contact* c = findContact(num);
    if (c) return c;
    if (s_contactN >= CONTACT_MAX) return nullptr;
    c = &s_contacts[s_contactN++];
    memset(c, 0, sizeof *c);
    c->num = num;
    c->used = true;
    return c;
}
static const char* nameFor(uint32_t num, char* buf, size_t cap) {
    Contact* c = findContact(num);
    if (c && c->shortName[0]) return c->shortName;
    snprintf(buf, cap, "%04X", (unsigned)(num & 0xFFFF));
    return buf;
}

// -------- incoming messages + inbox (task writes, loop reads) --------
QueueHandle_t s_inboxQ = nullptr;        // Message: the arrival toast
Message       s_last   = {};
Message       s_inbox[INBOX_N];          // newest first, for the list screen
uint8_t       s_inboxHead = 0;
uint8_t       s_inboxLen  = 0;
static void inboxPush(const Message& m) {
    s_inbox[s_inboxHead] = m;
    s_inboxHead = (uint8_t)((s_inboxHead + 1) % INBOX_N);
    if (s_inboxLen < INBOX_N) s_inboxLen++;
}

// -------- outgoing requests (loop writes, task reads) --------
struct Req {
    enum Kind : uint8_t { CONNECT, DISCONNECT, SEND, SCAN_ON, SCAN_OFF, TRACE } kind;
    uint8_t  channel;
    uint32_t to;              // BROADCAST_TO for a channel message, else a node
    uint32_t pid;             // our MeshPacket.id, pre-generated with the inbox copy
    uint32_t reply;           // Data.reply_id: 0, or the message this answers
    char     text[TEXT_MAX + 1];
};
QueueHandle_t s_reqQ = nullptr;
TaskHandle_t  s_task = nullptr;

// -------- FromNum doorbell: a monotonic count of FromRadio messages --------
volatile uint32_t s_fromNum   = 0;
volatile bool     s_fromNumNew= false;
uint32_t          s_lastRead  = 0;
uint32_t          s_lastPoll  = 0;

NimBLEClient*              s_client = nullptr;
NimBLERemoteCharacteristic* s_toRadio = nullptr;
NimBLERemoteCharacteristic* s_fromRadio = nullptr;

uint32_t s_ownNodeNum = 0;
bool     s_haveComplete = false;

// -------- send pacing + delivery tracking (task writes, loop takes) --------
// The node drops a second text inside 2 s and only has a 3-slot BLE intake
// queue, so the task spaces text writes (SEND_PACE_MS) and remembers the ids
// it put on the air. QueueStatus and Routing replies name those ids back.
uint32_t s_lastTextAt = 0;
volatile SendResult s_sendResult = SendResult::NONE;
uint32_t s_pendingIds[8] = {0};
uint8_t  s_pendingN = 0;
static void pendingRemember(uint32_t id) {
    if (!id) return;
    for (uint8_t i = 0; i < s_pendingN; i++) if (s_pendingIds[i] == id) return;
    if (s_pendingN < 8) s_pendingIds[s_pendingN++] = id;
    else { memmove(s_pendingIds, s_pendingIds + 1, 7 * sizeof(uint32_t)); s_pendingIds[7] = id; }
}
static bool pendingMatches(uint32_t id) {
    if (!id) return false;
    for (uint8_t i = 0; i < s_pendingN; i++) if (s_pendingIds[i] == id) return true;
    return false;
}
static void pendingDrop(uint32_t id) {
    for (uint8_t i = 0; i < s_pendingN; i++) if (s_pendingIds[i] == id) {
        if (i + 1 < s_pendingN) memmove(s_pendingIds + i, s_pendingIds + i + 1, (s_pendingN - i - 1) * sizeof(uint32_t));
        s_pendingN--; return;
    }
}

// -------- the bound node (auto-reconnect) --------
// `s_auto` is the standing instruction to be connected; it survives attempts
// and losses and is only cleared by a deliberate drop. `s_boundMac` is in the
// MSB-first order NimBLEAddress wants (see detection.cpp's reversal).
volatile bool s_auto      = false;
volatile bool s_pickSingle = false;   // auto, nothing bound: take a lone node
bool          s_haveBound = false;
uint8_t       s_boundMac[6] = {0,0,0,0,0,0};
uint8_t       s_boundType   = 0;
volatile uint32_t s_nextTryAt = 0;
volatile uint32_t s_scanSince = 0;
constexpr uint32_t RETRY_MS = 5000;
// How long to let a scan settle before trusting that exactly one node is all
// there is -- a second radio can take a moment to advertise.
constexpr uint32_t PICK_SETTLE_MS = 6000;
// The node we actually got in: remembered so a successful connection can be
// bound to NVS by the caller, whoever started it.
uint8_t       s_linkedMac[6] = {0,0,0,0,0,0};
uint8_t       s_linkedType   = 0;
volatile bool s_linkedValid  = false;

// =====================================================================
//  protobuf, by hand. Only the handful of fields this feature needs.
// =====================================================================
struct Pb {
    const uint8_t* p; size_t n; size_t i; bool ok;
};
static bool pbVarint(Pb& b, uint64_t& v) {
    v = 0; int shift = 0;
    while (b.i < b.n && shift < 64) {
        uint8_t c = b.p[b.i++];
        v |= (uint64_t)(c & 0x7F) << shift;
        if (!(c & 0x80)) return true;
        shift += 7;
    }
    b.ok = false; return false;
}
static bool pbTag(Pb& b, uint32_t& field, uint32_t& wire) {
    uint64_t k;
    if (!pbVarint(b, k)) return false;
    field = (uint32_t)(k >> 3); wire = (uint32_t)(k & 7);
    return true;
}
static bool pbSkip(Pb& b, uint32_t wire) {
    uint64_t v;
    switch (wire) {
        case 0: return pbVarint(b, v);
        case 1: if (b.i + 8 > b.n) { b.ok = false; return false; } b.i += 8; return true;
        case 2: { if (!pbVarint(b, v)) return false; if (b.i + v > b.n) { b.ok = false; return false; } b.i += (size_t)v; return true; }
        case 5: if (b.i + 4 > b.n) { b.ok = false; return false; } b.i += 4; return true;
        default: b.ok = false; return false;
    }
}
static bool pbSub(Pb& b, Pb& sub) {
    uint64_t len;
    if (!pbVarint(b, len)) return false;
    if (b.i + len > b.n) { b.ok = false; return false; }
    sub.p = b.p + b.i; sub.n = (size_t)len; sub.i = 0; sub.ok = true;
    b.i += (size_t)len;
    return true;
}
static bool pbBytes(Pb& b, const uint8_t*& data, size_t& len) {
    uint64_t l;
    if (!pbVarint(b, l)) return false;
    if (b.i + l > b.n) { b.ok = false; return false; }
    data = b.p + b.i; len = (size_t)l; b.i += (size_t)l;
    return true;
}
// fixed32 (wire 5), little-endian: what current firmware uses for node
// numbers and packet ids (MeshPacket.from/to/id, Data.request_id/reply_id,
// NodeInfo.last_heard). Older firmware sent the same fields as varints, so
// the parsers below accept both wires.
static bool pbFixed32(Pb& b, uint32_t& v) {
    if (b.i + 4 > b.n) { b.ok = false; return false; }
    v = (uint32_t)b.p[b.i] | ((uint32_t)b.p[b.i + 1] << 8) |
        ((uint32_t)b.p[b.i + 2] << 16) | ((uint32_t)b.p[b.i + 3] << 24);
    b.i += 4;
    return true;
}
static bool pbU32either(Pb& b, uint32_t wire, uint32_t& out) {
    if (wire == 0) { uint64_t v; if (!pbVarint(b, v)) return false; out = (uint32_t)v; return true; }
    if (wire == 5) return pbFixed32(b, out);
    b.ok = false; return false;
}

// ---- writer ----
struct Pbw { uint8_t* p; size_t n; size_t i; };
static void wVarint(Pbw& w, uint64_t v) {
    while (v >= 0x80 && w.i < w.n) { w.p[w.i++] = (uint8_t)(v | 0x80); v >>= 7; }
    if (w.i < w.n) w.p[w.i++] = (uint8_t)v;
}
static void wTag(Pbw& w, uint32_t field, uint32_t wire) { wVarint(w, ((uint64_t)field << 3) | wire); }
static void wVarintField(Pbw& w, uint32_t field, uint64_t v) { wTag(w, field, 0); wVarint(w, v); }
// fixed32 field (wire 5): node numbers and packet ids on current firmware.
static void wFixed32Field(Pbw& w, uint32_t field, uint32_t v) {
    wTag(w, field, 5);
    if (w.i + 4 <= w.n) { w.p[w.i++] = (uint8_t)v; w.p[w.i++] = (uint8_t)(v >> 8); w.p[w.i++] = (uint8_t)(v >> 16); w.p[w.i++] = (uint8_t)(v >> 24); }
    else w.i = w.n;
}
static void wBytesField(Pbw& w, uint32_t field, const uint8_t* d, size_t len) {
    wTag(w, field, 2); wVarint(w, len);
    size_t c = (w.i + len <= w.n) ? len : (w.n - w.i);
    if (c) { memcpy(w.p + w.i, d, c); w.i += c; }
}
static void wStringField(Pbw& w, uint32_t field, const char* s) {
    wBytesField(w, field, (const uint8_t*)s, strlen(s));
}

// =====================================================================
// =====================================================================
//  parsing a FromRadio
// =====================================================================
// User: id=1, long_name=2, short_name=3, macaddr=4, hw_model=5,
// is_licensed=6, role=7, public_key=8.
static void parseUser(Pb b, Contact& c) {
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 2 && w == 2) { const uint8_t* d; size_t l; if (pbBytes(b, d, l)) { size_t n = l < sizeof(c.longName) - 1 ? l : sizeof(c.longName) - 1; memcpy(c.longName, d, n); c.longName[n] = '\0'; } }
        else if (f == 3 && w == 2) { const uint8_t* d; size_t l; if (pbBytes(b, d, l)) { size_t n = l < sizeof(c.shortName) - 1 ? l : sizeof(c.shortName) - 1; memcpy(c.shortName, d, n); c.shortName[n] = '\0'; } }
        else if (f == 7 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; c.role = (uint8_t)v; }
        else if (f == 8 && w == 2) {
            const uint8_t* d; size_t l;
            if (pbBytes(b, d, l)) {
                c.hasKey = (l > 0);
                size_t n = l < sizeof(c.key) ? l : sizeof(c.key);
                memcpy(c.key, d, n);
                c.keyLen = (uint8_t)n;
            }
        }
        else if (!pbSkip(b, w)) break;
    }
}
static void parseNodeInfo(Pb b) {
    uint32_t num = 0; uint32_t lastHeard = 0; Contact tmp;
    memset(&tmp, 0, sizeof tmp);
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 1 && (w == 0 || w == 5)) { if (!pbU32either(b, w, num)) break; }
        else if (f == 2 && w == 2) { Pb u; if (pbSub(b, u)) parseUser(u, tmp); }
        else if (f == 5 && (w == 0 || w == 5)) { if (!pbU32either(b, w, lastHeard)) break; }
        else if (!pbSkip(b, w)) break;
    }
    Contact* c = upsertContact(num);
    if (!c) return;
    if (tmp.shortName[0]) { strncpy(c->shortName, tmp.shortName, sizeof(c->shortName) - 1); c->shortName[sizeof(c->shortName) - 1] = '\0'; }
    if (tmp.longName[0])  { strncpy(c->longName,  tmp.longName,  sizeof(c->longName)  - 1); c->longName[sizeof(c->longName) - 1] = '\0'; }
    c->role = tmp.role;
    if (tmp.hasKey) {
        c->hasKey = true;
        if (tmp.keyLen) { memcpy(c->key, tmp.key, tmp.keyLen); c->keyLen = tmp.keyLen; }
    }
    if (lastHeard) c->lastHeard = lastHeard;
}
// Channel: index=1, settings=2, role=3. ChannelSettings: channel_num=1,
// psk=2, name=3.
static void parseChannel(Pb b) {
    uint32_t index = 0, role = 0; char name[13] = ""; bool hasPsk = false;
    uint8_t psk[32] = {0}; size_t pskLen = 0;
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 1 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; index = (uint32_t)v; }
        else if (f == 3 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; role = (uint32_t)v; }
        else if (f == 2 && w == 2) {  // ChannelSettings
            Pb s;
            if (pbSub(b, s)) {
                while (s.ok && s.i < s.n) {
                    uint32_t sf, sw;
                    if (!pbTag(s, sf, sw)) break;
                    if (sf == 3 && sw == 2) { const uint8_t* d; size_t l; if (pbBytes(s, d, l)) { size_t n = l < sizeof(name) - 1 ? l : sizeof(name) - 1; memcpy(name, d, n); name[n] = '\0'; } }
                    else if (sf == 2 && sw == 2) {
                        const uint8_t* d; size_t l;
                        if (pbBytes(s, d, l)) {
                            hasPsk = (l > 0);
                            pskLen = l < sizeof(psk) ? l : sizeof(psk);
                            memcpy(psk, d, pskLen);
                        }
                    }
                    else if (!pbSkip(s, sw)) break;
                }
            }
        }
        else if (!pbSkip(b, w)) break;
    }
    // The PRIMARY channel's universal public key. Meshtastic's default primary
    // channel is the shared "LongFast" one, and its PSK is the single byte 0x01
    // (base64 "AQ==") -- what every stock node ships with. A node reports it as
    // an empty psk when the default is in force, so fill it in: without it the
    // channel would read as unencrypted, which it is not.
    if (role == 1 && pskLen == 0) { psk[0] = 0x01; pskLen = 1; hasPsk = true; }
    if (index < CHAN_MAX) {
        s_chans[index].index = (uint8_t)index;
        s_chans[index].role = (uint8_t)role;
        s_chans[index].hasPsk = hasPsk;
        s_chans[index].pskLen = (uint8_t)pskLen;
        if (pskLen) memcpy(s_chans[index].psk, psk, pskLen);
        s_chans[index].present = true;
        // A freshly synced primary notifies the main screen; secondaries
        // stay quiet until the user rings their bell. Do not clobber a
        // choice the user already made this session.
        static bool s_notifyTouched[CHAN_MAX] = {false};
        if (!s_notifyTouched[index]) {
            s_notify[index] = (role == 1);
            s_notifyTouched[index] = true;
        }
        if (name[0]) { strncpy(s_chans[index].name, name, sizeof(s_chans[index].name) - 1); s_chans[index].name[sizeof(s_chans[index].name) - 1] = '\0'; }
        else if (role == 1) snprintf(s_chans[index].name, sizeof(s_chans[index].name), "PRIMARY");
        else if (role == 0) snprintf(s_chans[index].name, sizeof(s_chans[index].name), "OFF");
        else                snprintf(s_chans[index].name, sizeof(s_chans[index].name), "CH%u", (unsigned)index);
        if (index + 1 > s_chanN) s_chanN = (uint8_t)(index + 1);
    }
}
static void pushIncoming(uint32_t fromNum, uint32_t toNum, uint8_t channel,
                         const uint8_t* text, size_t len,
                         uint32_t msgId, uint8_t hops, uint32_t relayNode) {
    Message m;
    memset(&m, 0, sizeof m);
    m.have = true; m.unread = true; m.outgoing = false;
    m.channel = channel;
    m.fromNum = fromNum;
    m.toNum = toNum;
    m.direct = (toNum != 0xFFFFFFFFu && toNum != 0);
    m.at = millis();
    m.msgId = msgId;
    m.pktId = 0;
    m.status = MsgStatus::DELIVERED;   // incoming: already here; hops below
    m.hops = hops;
    m.relayNode = relayNode;
    char nb[8];
    strncpy(m.from, nameFor(fromNum, nb, sizeof nb), sizeof(m.from) - 1);
    size_t c = len < TEXT_MAX ? len : TEXT_MAX;
    memcpy(m.body, text, c); m.body[c] = '\0';
    s_last = m;
    inboxPush(m);
    if (s_inboxQ) xQueueSendToFront(s_inboxQ, &m, 0);   // newest first
    Serial.printf("[meshlink] RX %s ch%u from %s: %s\n", m.direct ? "DM" : "ch", (unsigned)channel, m.from, m.body);
}
// Find our outgoing inbox copy by packet id, for status updates.
static Message* findByPid(uint32_t pid) {
    if (!pid) return nullptr;
    for (uint8_t i = 0; i < s_inboxLen; i++) {
        int idx = (int)s_inboxHead - 1 - (int)i;
        while (idx < 0) idx += INBOX_N;
        Message& m = s_inbox[idx % INBOX_N];
        if (m.outgoing && m.pktId == pid) return &m;
    }
    return nullptr;
}
// -------- traceroute state (task writes, loop reads) --------
static TraceResult s_trace = {};
constexpr uint32_t TRACE_TIMEOUT_MS = 30000;
// RouteDiscovery: route=1 fixed32, snr_towards=2 int32, route_back=3
// fixed32, snr_back=4 int32 -- each packed or unpacked on the wire.
static void parseTraceReply(uint32_t fromNum, uint32_t toNum, const uint8_t* p, size_t n) {
    if (!s_trace.active || !s_trace.waiting || fromNum != s_trace.target) return;
    if (toNum != s_ownNodeNum && toNum != 0 && toNum != BROADCAST_TO) return;
    Pb b{ p, n, 0, true };
    uint8_t rn = 0;
    int8_t sn[8] = {0};
    uint8_t snN = 0;
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if ((f == 1 || f == 3) && (w == 5 || w == 2)) {
            // Unpacked fixed32 entries, or one packed run of them.
            const uint8_t* run = nullptr; size_t runN = 0;
            uint32_t one = 0;
            if (w == 5) { if (!pbFixed32(b, one)) break; }
            else { if (!pbBytes(b, run, runN)) break; }
            uint32_t vals[8]; uint8_t vn = 0;
            if (w == 5) { vals[0] = one; vn = 1; }
            else {
                Pb pk{ run, runN, 0, true };
                while (pk.ok && pk.i < pk.n && vn < 8) { uint32_t v; if (!pbFixed32(pk, v)) break; vals[vn++] = v; }
            }
            if (f == 1) {
                for (uint8_t i = 0; i < vn && rn < 8; i++) s_trace.route[rn++] = vals[i];
                s_trace.n = rn;
            }
        } else if ((f == 2 || f == 4) && (w == 0 || w == 2)) {
            int32_t vals[8]; uint8_t vn = 0;
            if (w == 0) {
                uint64_t v; if (!pbVarint(b, v)) break;
                vals[0] = (int32_t)(int64_t)v; vn = 1;
            } else {
                const uint8_t* run = nullptr; size_t runN = 0;
                if (!pbBytes(b, run, runN)) break;
                Pb pk{ run, runN, 0, true };
                while (pk.ok && pk.i < pk.n && vn < 8) {
                    uint64_t v; if (!pbVarint(pk, v)) break;
                    vals[vn++] = (int32_t)(int64_t)v;
                }
            }
            if (f == 2) {
                for (uint8_t i = 0; i < vn && snN < 8; i++) sn[snN++] = (int8_t)vals[i];
                for (uint8_t i = 0; i < snN; i++) s_trace.snr[i] = sn[i];
            }
        }
        else if (!pbSkip(b, w)) break;
    }
    if (s_trace.n || snN) {
        // A route, or SNRs with an empty route (a direct neighbour answers
        // with no hops to list) -- either way the peer is reachable.
        s_trace.waiting = false;
        s_trace.replied = true;
        Serial.printf("[meshlink] TRACE reply from %08X: %u hops\n", (unsigned)fromNum, (unsigned)s_trace.n);
    }
}
static void parseMeshPacket(Pb b) {
    uint32_t fromNum = 0, toNum = 0; uint8_t channel = 0;
    uint32_t portnum = 0; const uint8_t* payload = nullptr; size_t plen = 0;
    uint32_t reqId = 0, reqOf = 0;
    uint32_t hopLimit = 0, hopStart = 0, relayNode = 0;
    bool hasHopLimit = false, hasHopStart = false;
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 1 && (w == 0 || w == 5)) { if (!pbU32either(b, w, fromNum)) break; }
        else if (f == 2 && (w == 0 || w == 5)) { if (!pbU32either(b, w, toNum)) break; }
        else if (f == 3 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; channel = (uint8_t)v; }
        else if (f == 6 && (w == 0 || w == 5)) { if (!pbU32either(b, w, reqId)) break; }
        else if (f == 9 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; hopLimit = (uint32_t)v; hasHopLimit = true; }
        else if (f == 15 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; hopStart = (uint32_t)v; hasHopStart = true; }
        else if (f == 19 && (w == 0 || w == 5)) { if (!pbU32either(b, w, relayNode)) break; }
        else if (f == 4 && w == 2) {  // Data
            Pb d;
            if (pbSub(b, d)) {
                while (d.ok && d.i < d.n) {
                    uint32_t df, dw;
                    if (!pbTag(d, df, dw)) break;
                    if (df == 1 && dw == 0) { uint64_t v; if (!pbVarint(d, v)) break; portnum = (uint32_t)v; }
                    else if (df == 2 && dw == 2) { if (!pbBytes(d, payload, plen)) break; }
                    else if (df == 6 && (dw == 0 || dw == 5)) { if (!pbU32either(d, dw, reqOf)) break; }
                    else if (!pbSkip(d, dw)) break;
                }
            }
        }
        else if (!pbSkip(b, w)) break;
    }
    uint8_t hops = 0;
    if (hasHopStart && hasHopLimit && hopStart >= hopLimit && hopStart - hopLimit < 16)
        hops = (uint8_t)(hopStart - hopLimit);
    if (portnum == 1 /* TEXT_MESSAGE_APP */ && payload && plen)
        pushIncoming(fromNum, toNum, channel, payload, plen, reqId, hops, relayNode);
    else if (portnum == 70 /* TRACEROUTE_APP */ && payload != nullptr)
        parseTraceReply(fromNum, toNum, payload, plen);
    else if (portnum == 4 /* NODEINFO_APP */ && payload && plen && fromNum) {
        // A live identity broadcast: the User message itself is the payload.
        // This is how keys and names arrive between config dumps -- without
        // it a contact met after connect stays keyless and unnreachable.
        Contact* c = upsertContact(fromNum);
        if (c) {
            Contact tmp; memset(&tmp, 0, sizeof tmp);
            Pb u{ payload, plen, 0, true };
            parseUser(u, tmp);
            if (tmp.shortName[0]) { strncpy(c->shortName, tmp.shortName, sizeof(c->shortName) - 1); c->shortName[sizeof(c->shortName) - 1] = '\0'; }
            if (tmp.longName[0])  { strncpy(c->longName,  tmp.longName,  sizeof(c->longName)  - 1); c->longName[sizeof(c->longName) - 1] = '\0'; }
            if (tmp.role) c->role = tmp.role;
            if (tmp.hasKey) {
                c->hasKey = true;
                if (tmp.keyLen) { memcpy(c->key, tmp.key, tmp.keyLen); c->keyLen = tmp.keyLen; }
            }
        }
    }
    else if (portnum == 5 /* ROUTING_APP */) {
        // A want_ack send comes back as a Routing packet addressed to us. An
        // error_reason of NONE means the mesh took it; anything else is the
        // reason it did not. decoded.request_id names the text it answers,
        // so match it against what we put on the air.
        uint32_t err = 0;
        if (payload && plen) {
            Pb r{ payload, plen, 0, true };
            while (r.ok && r.i < r.n) {
                uint32_t rf, rw;
                if (!pbTag(r, rf, rw)) break;
                if (rf == 3 && rw == 0) { uint64_t v; if (!pbVarint(r, v)) break; err = (uint32_t)v; }
                else if (!pbSkip(r, rw)) break;
            }
        }
        // Routing error codes (mesh.proto): NONE 0, PKI_FAILED 34,
        // PKI_UNKNOWN_PUBKEY 35, RATE_LIMIT_EXCEEDED 38,
        // PKI_SEND_FAIL_PUBLIC_KEY 39.
        if (pendingMatches(reqOf)) {
            pendingDrop(reqOf);
            Message* m = findByPid(reqOf);
            if (err == 0) {
                s_sendResult = SendResult::SENT;
                if (m) { m->status = MsgStatus::DELIVERED; m->tDone = millis(); m->routeErr = 0; }
            }
            else if (err == 38) s_sendResult = SendResult::RATE_LIMITED;
            else if (err == 34 || err == 35 || err == 39) s_sendResult = SendResult::PKI_FAILED;
            else s_sendResult = SendResult::FAILED;
            if (err != 0 && m) { m->status = MsgStatus::FAILED; m->tDone = millis(); m->routeErr = (uint8_t)err; }
        }
        Serial.printf("[meshlink] ROUTING ack id=%08X err=%u%s\n", (unsigned)reqId, (unsigned)err,
                      err ? " (NOT delivered)" : " (ok)");
    }
}
static void parseFromRadio(const uint8_t* d, size_t n) {
    Pb b{ d, n, 0, true };
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
#if MESH_COMPANION_AUTOSTART
        Serial.printf("[meshlink] FromRadio field=%u wire=%u len=%u\n", (unsigned)f, (unsigned)w, (unsigned)n);
#endif
        if (f == 2 && w == 2) {
            Pb p;
            if (pbSub(b, p)) {
#if MESH_COMPANION_AUTOSTART
                Serial.printf("[meshlink] PKT hex:");
                for (size_t k = 0; k < p.n && k < 40; k++) Serial.printf(" %02X", p.p[k]);
                Serial.println();
#endif
                parseMeshPacket(p);
            }
        }
        else if (f == 3 && w == 2) {  // MyNodeInfo
            Pb m;
            if (pbSub(b, m)) {
                while (m.ok && m.i < m.n) {
                    uint32_t mf, mw;
                    if (!pbTag(m, mf, mw)) break;
                    if (mf == 1 && mw == 0) { uint64_t v; if (!pbVarint(m, v)) break; s_ownNodeNum = (uint32_t)v; }
                    else if (!pbSkip(m, mw)) break;
                }
            }
        }
        else if (f == 4 && w == 2) { Pb ni; if (pbSub(b, ni)) parseNodeInfo(ni); }
        else if (f == 7 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; s_haveComplete = true; }
        else if (f == 10 && w == 2) { Pb c; if (pbSub(b, c)) parseChannel(c); }
        else if (f == 11 && w == 2) {  // QueueStatus: the node took (or refused) a send
            Pb q;
            if (pbSub(b, q)) {
                uint32_t freeSlots = 0, maxlen = 0, res = 0, pid = 0;
                while (q.ok && q.i < q.n) {
                    uint32_t qf, qw;
                    if (!pbTag(q, qf, qw)) break;
                    uint64_t v = 0;
                    if (qf == 1 && qw == 0) { if (!pbVarint(q, v)) break; res = (uint32_t)v; }
                    else if (qf == 2 && qw == 0) { if (!pbVarint(q, v)) break; freeSlots = (uint32_t)v; }
                    else if (qf == 3 && qw == 0) { if (!pbVarint(q, v)) break; maxlen = (uint32_t)v; }
                    else if (qf == 4 && qw == 0) { if (!pbVarint(q, v)) break; pid = (uint32_t)v; }
                    else if (!pbSkip(q, qw)) break;
                }
                // mesh_packet_id names the send this answers. A nonzero res is
                // the node refusing it outright; drop the pending id so a late
                // ack cannot resurrect it as delivered.
                if (pendingMatches(pid)) {
                    Message* m = findByPid(pid);
                    if (res == 0) {
                        s_sendResult = SendResult::QUEUED;
                        if (m) {
                            m->tQueued = millis();
                            // Broadcasts never get a Routing ack: acceptance
                            // is their terminal state. DMs wait for the ack.
                            if (m->direct) m->status = MsgStatus::QUEUED;
                            else { m->status = MsgStatus::IN_MESH; m->tDone = m->tQueued; }
                        }
                    }
                    else {
                        s_sendResult = SendResult::FAILED; pendingDrop(pid);
                        if (m) { m->status = MsgStatus::FAILED; m->tDone = millis(); m->routeErr = (uint8_t)(res + 100); }
                    }
                }
                Serial.printf("[meshlink] QUEUE res=%u free=%u/%u id=%08X\n",
                              (unsigned)res, (unsigned)freeSlots, (unsigned)maxlen, (unsigned)pid);
            }
        }
        else if (!pbSkip(b, w)) break;
    }
}

// =====================================================================
//  BLE
// =====================================================================
void fromNumNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (len >= 4) {
        uint32_t v = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                     ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
        s_fromNum = v;
        s_fromNumNew = true;
#if MESH_COMPANION_AUTOSTART
        Serial.printf("[meshlink] FromNum notify=%u\n", (unsigned)v);
#endif
    }
}

#if MESH_COMPANION_AUTOSTART
// The node mirrors its own firmware log to us over BLE. Reading it here is how
// we see *why* the node drops something, without a serial console on the node.
void logRadioNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (!len) return;
    char buf[256];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, data, n); buf[n] = '\0';
    for (size_t i = 0; i < n; i++) if (buf[i] == '\n' || buf[i] == '\r') buf[i] = ' ';
    Serial.printf("[nodelog] %s\n", buf);
}
#endif

// Meshtastic's Bluetooth pairing mode is FIXED_PIN (or RANDOM_PIN) by default:
// the node requires a bonded, encrypted link before it will hand over the mesh
// service. This is the PIN to enter -- the fixed one set on the node. It will
// move to a setting; for now it is the bench node's.
constexpr uint32_t kPairPin = 902100;

volatile int s_connStatus = 0;   // 0 pending, 1 connected, -1 failed
volatile int s_connReason = 0;
volatile int s_authStatus = 0;   // 0 pending, 1 encrypted, -1 failed

class ClientCb : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient*) override {
        s_connStatus = 1;
    }
    void onConnectFail(NimBLEClient*, int reason) override {
        Serial.printf("[meshlink] onConnectFail reason=%d\n", reason);
        s_connReason = reason;
        s_connStatus = -1;
    }
    void onDisconnect(NimBLEClient*, int reason) override {
        Serial.printf("[meshlink] onDisconnect reason=%d\n", reason);
        if (s_connStatus == 0) { s_connReason = reason; s_connStatus = -1; }
    }
    // The node displays its PIN; we are the keyboard and enter it.
    void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
        Serial.printf("[meshlink] passkey requested, entering %u\n", (unsigned)kPairPin);
        NimBLEDevice::injectPassKey(connInfo, kPairPin);
    }
    void onConfirmPasskey(NimBLEConnInfo& connInfo, uint32_t pin) override {
        Serial.printf("[meshlink] numeric compare %06u, accepting\n", (unsigned)pin);
        NimBLEDevice::injectConfirmPasskey(connInfo, true);
    }
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        s_authStatus = connInfo.isEncrypted() ? 1 : -1;
        Serial.printf("[meshlink] auth complete: encrypted=%d bonded=%d\n",
                      (int)connInfo.isEncrypted(), (int)connInfo.isBonded());
    }
};
ClientCb s_clientCb;
bool readOneFromRadio() {
    if (!s_fromRadio) return false;
    NimBLEAttValue v = s_fromRadio->readValue();
    if (v.size() == 0) return false;
    parseFromRadio(v.data(), v.size());
    return true;
}

void sendWantConfig() {
    uint8_t buf[16]; Pbw w{ buf, sizeof buf, 0 };
    wVarintField(w, 3, WANT_CONFIG_ID);       // ToRadio.want_config_id
    if (s_toRadio) s_toRadio->writeValue(buf, w.i, true);
}

// ToRadio.heartbeat (field 7, Heartbeat{nonce=1}). nonce 0 is a plain
// keepalive the node answers with a QueueStatus; nonce 1 asks it to re-broadcast
// our NodeInfo. Either way it proves the ToRadio write path end to end.
void sendHeartbeat(uint32_t nonce) {
    if (!s_toRadio) return;
    uint8_t hb[4]; Pbw h{ hb, sizeof hb, 0 };
    wVarintField(h, 1, nonce);                // Heartbeat.nonce
    uint8_t buf[8]; Pbw w{ buf, sizeof buf, 0 };
    wBytesField(w, 7, hb, h.i);               // ToRadio.heartbeat
    bool ok = s_toRadio->writeValue(buf, w.i, true);
    Serial.printf("[meshlink] BEAT nonce=%u %s\n", (unsigned)nonce, ok ? "sent" : "WRITE FAILED");
}

// The shared observer scan and a GATT link share one radio. A scan left
// running beside the connection starves its link-layer events until the
// supervision timer fires (disconnect reason 520 = HCI 0x08 Connection
// Timeout, seen every minute or so on the bench). So the scan runs only
// while hunting: off for the life of a link, back on for discovery and
// retries. Detection never restarts a stopped scan on its own, and leaving
// COMPANION mode (shutdown) puts it back for BROMESH object detection.
static void hwScanSet(bool on) {
    NimBLEScan* sc = NimBLEDevice::getScan();
    if (!sc) return;
    if (on) { if (!sc->isScanning()) sc->start(0, false, false); }
    else if (sc->isScanning()) sc->stop();
}

// A byte cap that never splits a UTF-8 character: back off the continuation
// bytes so a Russian letter is either sent whole or not at all.
static size_t utf8SafeLen(const char* s, size_t cap) {
    size_t n = strlen(s);
    if (n <= cap) return n;
    n = cap;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
    return n;
}

// One Meshtastic app packet on the air. `track` wires it into delivery
// tracking (pending ids + the inbox copy's status); traces skip it --
// their Routing acks would otherwise masquerade as text deliveries.
static bool sendAppNow(uint32_t toNum, uint8_t channel, uint32_t portnum,
                       const uint8_t* payload, size_t plen,
                       bool wantAck, uint32_t replyId, bool wantResponse,
                       uint32_t pid, bool track) {
    if (!s_toRadio) return false;
    uint8_t data[8 + TEXT_MAX]; Pbw d{ data, sizeof data, 0 };
    wVarintField(d, 1, portnum);                  // Data.portnum
    wBytesField(d, 2, payload ? payload : (const uint8_t*)"", plen);  // Data.payload
    if (wantResponse) wVarintField(d, 3, 1);      // Data.want_response
    if (replyId) wFixed32Field(d, 7, replyId);    // Data.reply_id (a reply/reaction)

    uint8_t pkt[16 + sizeof data]; Pbw p{ pkt, sizeof pkt, 0 };
    wFixed32Field(p, 1, s_ownNodeNum);        // MeshPacket.from (fixed32 on current firmware)
    wFixed32Field(p, 2, toNum);               // MeshPacket.to (broadcast or a node)
    wVarintField(p, 3, channel);               // MeshPacket.channel
    wBytesField(p, 4, data, d.i);              // MeshPacket.decoded
    wFixed32Field(p, 6, pid);                  // MeshPacket.id
    wVarintField(p, 9, 3);                     // MeshPacket.hop_limit
    // want_ack only makes sense point to point: nobody acks a broadcast, and
    // the flag on one would just promise a Routing reply that never comes.
    if (wantAck) wVarintField(p, 10, 1);

    uint8_t out[8 + sizeof pkt]; Pbw o{ out, sizeof out, 0 };
    wBytesField(o, 1, pkt, p.i);               // ToRadio.packet
#if MESH_COMPANION_AUTOSTART
    Serial.printf("[meshlink] TXRAW id=%08X len=%u\n", (unsigned)pid, (unsigned)o.i);
#endif
    bool ok = s_toRadio->writeValue(out, o.i, true);
    if (ok) {
        if (track) {
            pendingRemember(pid);
            s_lastTextAt = millis();
            if (Message* m = findByPid(pid)) { m->status = MsgStatus::SENT; m->tSent = millis(); }
        } else {
            // An untracked write still occupies the node's intake: pace the
            // next tracked text behind it.
            s_lastTextAt = millis();
        }
        Serial.printf("[meshlink] TX port%u ch%u to=%08X id=%08X\n",
                      (unsigned)portnum, (unsigned)channel, (unsigned)toNum, (unsigned)pid);
    } else {
        Serial.println("[meshlink] TX failed: write to ToRadio refused");
    }
    return ok;
}

static bool sendChannelTextNow(uint32_t toNum, uint8_t channel, const char* text, uint32_t replyId, uint32_t pid) {
    if (!text || !text[0]) return false;
    const size_t tlen = utf8SafeLen(text, TEXT_LIMIT);
    if (!tlen) return false;
    const bool dm = (toNum != BROADCAST_TO && toNum != 0);
    return sendAppNow(toNum, channel, 1 /* TEXT */, (const uint8_t*)text, tlen,
                      dm /* want_ack */, replyId, false /* want_response */, pid, true);
}

void doConnect(uint8_t index) {
    if (index >= s_nodeN || !s_client) return;
    s_state = State::CONNECTING;
    s_stateAt = millis();
    s_reason[0] = '\0';

    NimBLEAddress addr(s_nodes[index].mac, s_nodes[index].addrType);
    Serial.printf("[meshlink] connecting to %02X:%02X:%02X:%02X:%02X:%02X ...\n",
                  s_nodes[index].mac[0], s_nodes[index].mac[1], s_nodes[index].mac[2],
                  s_nodes[index].mac[3], s_nodes[index].mac[4], s_nodes[index].mac[5]);
    // The continuous observer scan owns the radio, and with it running the
    // initiating connect is not scheduled and times out (BLE_HS_ETIMEOUT).
    // Pause it for the attempt; on failure discovery resumes, on success it
    // stays off for the life of the link (see hwScanSet).
    hwScanSet(false);

    s_connStatus = 0;
    s_connReason = 0;
    bool ok = s_client->connect(addr, true, true, true);
    if (ok) {
        const uint32_t t0 = millis();
        while (s_connStatus == 0 && millis() - t0 < 12000) vTaskDelay(pdMS_TO_TICKS(50));
        ok = (s_connStatus == 1);
    }
    // The advertised address type is not always trustworthy, and a connect to
    // the wrong type times out. Try the other type once; remember what worked.
    if (!ok) {
        const uint8_t alt = s_nodes[index].addrType ^ 1;
        s_client->disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
        NimBLEAddress addr2(s_nodes[index].mac, alt);
        s_connStatus = 0; s_connReason = 0;
        bool ok2 = s_client->connect(addr2, true, true, true);
        if (ok2) {
            const uint32_t t0 = millis();
            while (s_connStatus == 0 && millis() - t0 < 12000) vTaskDelay(pdMS_TO_TICKS(50));
            ok2 = (s_connStatus == 1);
            if (ok2) s_nodes[index].addrType = alt;
        }
        ok = ok2;
    }
    if (!ok) {
        hwScanSet(s_scanning);
        s_state = State::ERROR;
        snprintf(s_reason, sizeof s_reason, "connect failed");
        Serial.printf("[meshlink] connect FAILED (reason=%d)\n", s_connReason);
        return;
    }

    // Meshtastic will not open its service until the link is encrypted, and
    // with a PIN that means bonding. secureConnection() starts it; the passkey
    // itself arrives in onPassKeyEntry above.
    memcpy(s_linkedMac, s_nodes[index].mac, 6);
    s_linkedType  = s_nodes[index].addrType;
    s_linkedValid = true;
    Serial.println("[meshlink] connected; securing link");
    s_authStatus = 0;
    s_client->secureConnection();
    {
        const uint32_t t0 = millis();
        while (s_authStatus == 0 && millis() - t0 < 12000) vTaskDelay(pdMS_TO_TICKS(50));
    }
    Serial.printf("[meshlink] auth status=%d\n", (int)s_authStatus);
    if (s_authStatus <= 0) {
        // -1: refused. 0: the 12 s wait ran out (secureConnection itself can
        // block ~30 s on a dying link) -- either way there is no encrypted
        // link, and without it the node hides the mesh service.
        hwScanSet(s_scanning);
        s_client->disconnect();
        s_state = State::ERROR;
        snprintf(s_reason, sizeof s_reason, s_authStatus < 0 ? "pair failed" : "pair timeout");
        return;
    }

    // Linked: the observer stays off until the drop (see hwScanSet).
    Serial.printf("[meshlink] discovering GATT (MTU=%u)\n", (unsigned)s_client->getMTU());
    NimBLERemoteService* svc = s_client->getService(NimBLEUUID(kMeshSvc));
    if (!svc) { snprintf(s_reason, sizeof s_reason, "no mesh service"); s_client->disconnect(); s_state = State::ERROR; return; }
    s_toRadio   = svc->getCharacteristic(NimBLEUUID(kToRadio));
    s_fromRadio = svc->getCharacteristic(NimBLEUUID(kFromRadio));
    NimBLERemoteCharacteristic* fromNum = svc->getCharacteristic(NimBLEUUID(kFromNum));
#if MESH_COMPANION_AUTOSTART
    NimBLERemoteCharacteristic* logRadio = svc->getCharacteristic(NimBLEUUID(kLogRadio));
    Serial.printf("[meshlink] chars: to=%p from=%p num=%p log=%p\n", (void*)s_toRadio, (void*)s_fromRadio, (void*)fromNum, (void*)logRadio);
#else
    Serial.printf("[meshlink] chars: to=%p from=%p num=%p\n", (void*)s_toRadio, (void*)s_fromRadio, (void*)fromNum);
#endif
    if (!s_toRadio || !s_fromRadio) { snprintf(s_reason, sizeof s_reason, "no chars"); s_client->disconnect(); s_state = State::ERROR; return; }
#if MESH_COMPANION_AUTOSTART
    if (logRadio) logRadio->subscribe(true, logRadioNotify);
#endif

    // Baseline the doorbell so the READY drain knows what is new.
    if (fromNum) {
        fromNum->subscribe(true, fromNumNotify);
        NimBLEAttValue v = fromNum->readValue();
        if (v.size() >= 4)
            s_lastRead = (uint32_t)v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
    }
    s_haveComplete = false;
    s_chanN = 0;
    s_contactN = 0;
    memset(s_chans, 0, sizeof s_chans);
    memset(s_contacts, 0, sizeof s_contacts);
    s_state = State::HANDSHAKE;
    s_stateAt = millis();
    sendWantConfig();
    Serial.println("[meshlink] want_config sent, syncing");
}

void doDisconnect() {
    if (s_client && s_client->isConnected()) s_client->disconnect();
    s_toRadio = s_fromRadio = nullptr;
    hwScanSet(s_scanning);
    s_state = s_scanning ? State::SCANNING : State::OFF;
}

void taskLoop(void*) {
    Req r;
    for (;;) {
        bool paced = false;
        while (!paced && s_reqQ && xQueueReceive(s_reqQ, &r, 0) == pdTRUE) {
            switch (r.kind) {
                case Req::CONNECT:    doConnect(r.channel); break;
                case Req::DISCONNECT: doDisconnect(); break;
                case Req::SEND: {
                    // Pace texts: the node drops a second text inside 2 s and
                    // its BLE intake holds three writes. Requeue to the front
                    // so order is kept, then yield so other work still runs.
                    if (s_lastTextAt) {
                        int32_t wait = (int32_t)(s_lastTextAt + SEND_PACE_MS - millis());
                        if (wait > 0) {
                            xQueueSendToFront(s_reqQ, &r, 0);
                            paced = true;
                            break;
                        }
                    }
                    sendChannelTextNow(r.to, r.channel, r.text, r.reply, r.pid);
                    break;
                }
                case Req::SCAN_ON:    s_scanning = true;  if (s_state == State::OFF) s_state = State::SCANNING; break;
                case Req::SCAN_OFF:   s_scanning = false; if (s_state == State::SCANNING) s_state = State::OFF; break;
                case Req::TRACE: {
                    // An empty RouteDiscovery on TRACEROUTE_APP, like the
                    // official clients send it; the mesh fills the path in.
                    // Untracked: its Routing acks must not read as text
                    // deliveries. Paced like a text (shared intake).
                    if (s_state != State::READY || !r.to) {
                        s_trace.active = false; s_trace.waiting = false;
                        break;
                    }
                    if (s_lastTextAt) {
                        int32_t wait = (int32_t)(s_lastTextAt + SEND_PACE_MS - millis());
                        if (wait > 0) {
                            xQueueSendToFront(s_reqQ, &r, 0);
                            paced = true;
                            break;
                        }
                    }
                    const uint32_t tpid = esp_random() ? esp_random() : 1;
                    memset(&s_trace, 0, sizeof s_trace);
                    s_trace.active = true; s_trace.waiting = true;
                    s_trace.target = r.to; s_trace.at = millis();
                    if (!sendAppNow(r.to, 0, 70 /* TRACEROUTE_APP */, nullptr, 0,
                                    true /* want_ack */, 0, true /* want_response */, tpid, false))
                        { s_trace.active = false; s_trace.waiting = false; }
                    break;
                }
            }
        }

        if (s_state == State::HANDSHAKE) {
            // The node queued the whole config dump right after want_config;
            // config_complete_id is the last of it, so this never waits on an
            // empty queue.
            for (int i = 0; i < 64 && !s_haveComplete; i++) {
                if (!readOneFromRadio()) break;
            }
            if (s_haveComplete) {
                s_state = State::READY;
                s_stateAt = millis();
                s_lastRead = s_fromNum;
                Serial.printf("[meshlink] READY: own=%08X, %u channels, %u contacts\n",
                              (unsigned)s_ownNodeNum, (unsigned)s_chanN, (unsigned)s_contactN);
                // Sync, the way the official clients do on connect: the dump
                // above is the channels/contacts; a nonce-1 heartbeat asks the
                // node to re-broadcast our NodeInfo (so the mesh re-learns us),
                // and the READY drain below keeps live NodeInfos/texts flowing.
                // (There is no "missed messages" fetch in PhoneAPI -- a node
                // only streams live packets unless it runs Store & Forward.)
                sendHeartbeat(1);
#if MESH_COMPANION_AUTOSTART
                for (uint8_t ci = 0; ci < s_chanN; ci++)
                    Serial.printf("[meshlink]   ch%u role=%u psk=%d name='%s'\n",
                                  (unsigned)s_chans[ci].index, (unsigned)s_chans[ci].role,
                                  (int)s_chans[ci].hasPsk, s_chans[ci].name);
                for (uint8_t ci = 0; ci < s_contactN && ci < 8; ci++)
                    Serial.printf("[meshlink]   %08X '%s'/'%s' key=%d\n",
                                  (unsigned)s_contacts[ci].num, s_contacts[ci].shortName,
                                  s_contacts[ci].longName, (int)s_contacts[ci].hasKey);
#endif
            }
        } else if (s_state == State::READY) {
            // Drain whenever the doorbell rang, or on a slow poll. The notify
            // is an optimisation, not the only way in: a missed notification
            // must not strand a message, and after the config dump this is the
            // only place incoming texts and routing acks are read.
            if (s_fromNumNew || (millis() - s_lastPoll) > 400) {
                s_fromNumNew = false;
                s_lastPoll   = millis();
                for (int i = 0; i < 16; i++) {
                    if (!readOneFromRadio()) break;
                    s_lastRead++;
                }
            }
            // A periodic keepalive: the node answers with a QueueStatus, which
            // proves the ToRadio path and keeps the session alive.
            static uint32_t s_lastBeat = 0;
            if ((millis() - s_lastBeat) > 30000) {
                s_lastBeat = millis();
                sendHeartbeat(0);
            }
            if (s_client && !s_client->isConnected()) {
                s_reason[0] = '\0';
                snprintf(s_reason, sizeof s_reason, "lost node");
                s_state = State::ERROR;
                // The retry path below hunts by advert again, so discovery
                // gets the radio back now.
                hwScanSet(s_scanning);
            }
        }
        // THE BOUND NODE. While the board is told to be connected, a scan in
        // flight looks for that exact address and connects the moment it
        // appears; a failed or lost link is retried on a slow clock, so a
        // node that is switched off is not hammered. Everything here is a
        // no-op in BROMESH, where s_auto is never set.
        if (s_auto && s_haveBound) {
            if (s_state == State::SCANNING) {
                for (uint8_t i = 0; i < s_nodeN; i++) {
                    if (memcmp(s_nodes[i].mac, s_boundMac, 6) == 0) { doConnect(i); break; }
                }
            } else if ((s_state == State::ERROR || s_state == State::OFF) &&
                       (int32_t)(millis() - s_nextTryAt) >= 0) {
                s_nextTryAt = millis() + RETRY_MS;
                startScan();
            }
        } else if (s_auto && s_pickSingle) {
            if (s_state == State::SCANNING) {
                if (s_nodeN == 1 && (millis() - s_scanSince) > PICK_SETTLE_MS) {
                    memcpy(s_boundMac, s_nodes[0].mac, 6);
                    s_boundType = s_nodes[0].addrType;
                    s_haveBound = true;
                    Serial.printf("[meshlink] AUTO: lone node %02X:%02X:%02X:%02X:%02X:%02X, binding\n",
                                  s_nodes[0].mac[0], s_nodes[0].mac[1], s_nodes[0].mac[2],
                                  s_nodes[0].mac[3], s_nodes[0].mac[4], s_nodes[0].mac[5]);
                }
            } else if ((s_state == State::ERROR || s_state == State::OFF) &&
                       (int32_t)(millis() - s_nextTryAt) >= 0) {
                s_nextTryAt = millis() + RETRY_MS;
                startScan();
            }
        }
#if MESH_COMPANION_AUTOSTART
        // Bench build: bind the first node seen and hand off to the ordinary
        // auto-reconnect path above, so what the bench exercises is exactly
        // what a real board does after a reboot.
        if (!s_auto && s_nodeN > 0) {
            Serial.printf("[meshlink] AUTOSTART: binding node %02X:%02X:%02X:%02X:%02X:%02X\n",
                          s_nodes[0].mac[0], s_nodes[0].mac[1], s_nodes[0].mac[2],
                          s_nodes[0].mac[3], s_nodes[0].mac[4], s_nodes[0].mac[5]);
            memcpy(s_boundMac, s_nodes[0].mac, 6);
            s_boundType = s_nodes[0].addrType;
            s_haveBound = true;
            s_auto      = true;
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

} // namespace

// =====================================================================
//  public API
// =====================================================================
const char* targetLabel(Target t) {
    return t == Target::MESHCORE ? "MESHCORE" : "MESHTASTIC";
}

void begin() {
    if (s_task) return;
    s_nodeMux = xSemaphoreCreateMutex();
    s_inboxQ  = xQueueCreate(INBOX_N, sizeof(Message));
    s_reqQ    = xQueueCreate(8, sizeof(Req));
    memset(s_chans, 0, sizeof s_chans);
    memset(s_contacts, 0, sizeof s_contacts);
    const NimBLEAddress& a = NimBLEDevice::getAddress();
    memcpy((void*)s_ownMac, a.getBase()->val, 6);
    Serial.printf("[meshlink] own BLE addr %s type=%d\n", a.toString().c_str(), (int)a.getType());
    s_client = NimBLEDevice::createClient();
    s_client->setConnectTimeout(8000);
    s_client->setClientCallbacks(&s_clientCb, false);
    // We are the keyboard: the node displays a PIN and we type it. Bonding,
    // MITM and secure connections, matching what Meshtastic's node asks for.
    NimBLEDevice::setSecurityIOCap(4 /* BLE_HS_IO_KEYBOARD_DISPLAY */);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityPasskey(kPairPin);
    NimBLEDevice::setMTU(517);   // long ToRadio writes need a big ATT MTU
    xTaskCreatePinnedToCore(taskLoop, "meshlink", 8192, nullptr, 1, &s_task, 0);
#if MESH_COMPANION_AUTOSTART
    Serial.println("[meshlink] AUTOSTART: scanning + auto-connect");
    startScan();
#endif
}

void tick(uint32_t now) {
    (void)now;
}

void shutdown() {
    s_auto = false;
    s_pickSingle = false;
    if (s_reqQ) { Req r; memset(&r, 0, sizeof r); r.kind = Req::DISCONNECT; xQueueSend(s_reqQ, &r, 0); }
    s_scanning = false;
    // Back to BROMESH object detection, which never restarts a stopped scan.
    hwScanSet(true);
}

void setTarget(Target t) { s_target = t; }
Target target()          { return s_target; }

void startScan() {
    s_scanning = true;
    s_scanSince = millis();
    hwScanSet(true);   // a stopped observer (see hwScanSet) finds nothing
    if (s_state == State::OFF || s_state == State::ERROR) s_state = State::SCANNING;
    if (s_reqQ) { Req r; memset(&r, 0, sizeof r); r.kind = Req::SCAN_ON; xQueueSend(s_reqQ, &r, 0); }
}
void stopScan() {
    s_scanning = false;
    if (s_state == State::SCANNING) s_state = State::OFF;
    if (s_reqQ) { Req r; memset(&r, 0, sizeof r); r.kind = Req::SCAN_OFF; xQueueSend(s_reqQ, &r, 0); }
}
uint8_t nodeCount() { return s_nodeN; }
const Node& nodeAt(uint8_t i) { return s_nodes[i < s_nodeN ? i : 0]; }

void connect(uint8_t index) {
    if (!s_reqQ) return;
    Req r; memset(&r, 0, sizeof r);
    r.kind = Req::CONNECT; r.channel = index;
    xQueueSend(s_reqQ, &r, 0);
}
void disconnect() {
    s_auto = false;                 // a deliberate drop is not undone behind the user
    s_pickSingle = false;
    if (!s_reqQ) return;
    Req r; memset(&r, 0, sizeof r); r.kind = Req::DISCONNECT; xQueueSend(s_reqQ, &r, 0);
}
void autoConnect(const uint8_t mac[6], uint8_t addrType) {
    memcpy(s_boundMac, mac, 6);
    s_boundType = addrType & 1;
    s_haveBound = true;
    s_pickSingle = false;
    s_auto      = true;
    s_nextTryAt = 0;
    startScan();
}
void autoStart() {
    s_haveBound  = false;
    s_pickSingle = true;
    s_auto       = true;
    s_nextTryAt  = 0;
    startScan();
}
bool autoActive() { return s_auto; }
bool linkedMac(uint8_t mac[6], uint8_t* addrType) {
    if (!s_linkedValid) return false;
    memcpy(mac, s_linkedMac, 6);
    if (addrType) *addrType = s_linkedType;
    return true;
}
State state() { return s_state; }
bool  connected() { return s_state == State::READY; }

const char* stateLabel() {
    switch (s_state) {
        case State::OFF:        return "OFF";
        case State::SCANNING:   return "SEARCH";
        case State::CONNECTING: return "LINK";
        case State::HANDSHAKE:  return "SYNC";
        case State::READY:      return "READY";
        case State::ERROR:      return s_reason[0] ? s_reason : "ERR";
    }
    return "?";
}

uint8_t        channelCount() { return s_chanN; }
const Channel& channelAt(uint8_t i) { return s_chans[i < CHAN_MAX ? i : 0]; }
void           setSendChannel(uint8_t i) { s_sendChan = i; }
uint8_t        sendChannel() { return s_sendChan; }
bool           channelNotify(uint8_t idx) { return idx < CHAN_MAX ? s_notify[idx] : false; }
void           setChannelNotify(uint8_t idx, bool on) { if (idx < CHAN_MAX) s_notify[idx] = on; }

bool sendChannelText(uint8_t channel, const char* text, uint32_t replyId) {
    if (s_state != State::READY || !s_reqQ) { s_sendResult = SendResult::NOT_READY; return false; }
    if (!text || !text[0]) return false;
    const size_t tlen = utf8SafeLen(text, TEXT_LIMIT);
    if (!tlen) return false;
    Req r; memset(&r, 0, sizeof r);
    r.kind = Req::SEND; r.channel = channel; r.to = BROADCAST_TO;
    r.pid = esp_random() ? esp_random() : 1;
    r.reply = replyId;
    memcpy(r.text, text, tlen); r.text[tlen] = '\0';
    // Visible immediately as PENDING; the task flips it to SENT on write.
    Message m; memset(&m, 0, sizeof m);
    m.have = true; m.unread = false; m.outgoing = true;
    m.channel = channel; m.toNum = BROADCAST_TO; m.fromNum = s_ownNodeNum;
    m.direct = false; m.at = millis();
    m.msgId = r.pid; m.pktId = r.pid; m.status = MsgStatus::PENDING;
    strncpy(m.from, "YOU", sizeof(m.from) - 1);
    memcpy(m.body, text, tlen); m.body[tlen] = '\0';
    if (xQueueSend(s_reqQ, &r, 0) != pdTRUE) {
        m.status = MsgStatus::FAILED; m.tDone = millis(); m.routeErr = 200;  // local queue full
        s_sendResult = SendResult::FAILED;
    }
    s_last = m;
    inboxPush(m);
    if (s_inboxQ) xQueueSendToFront(s_inboxQ, &m, 0);
    return m.status != MsgStatus::FAILED;
}

bool sendDirectText(uint32_t toNum, const char* text, uint32_t replyId) {
    if (s_state != State::READY || !s_reqQ) { s_sendResult = SendResult::NOT_READY; return false; }
    if (!text || !text[0] || !toNum) return false;
    // No public key, no PKI: the node cannot encrypt a DM to this contact and
    // fails it outright. Refuse early so the UI can say why.
    if (!contactHasKey(toNum)) { s_sendResult = SendResult::NO_KEY; return false; }
    const size_t tlen = utf8SafeLen(text, TEXT_LIMIT);
    if (!tlen) return false;
    Req r; memset(&r, 0, sizeof r);
    r.kind = Req::SEND; r.channel = 0; r.to = toNum;   // DMs go on the primary channel
    r.pid = esp_random() ? esp_random() : 1;
    r.reply = replyId;
    memcpy(r.text, text, tlen); r.text[tlen] = '\0';
    Message m; memset(&m, 0, sizeof m);
    m.have = true; m.unread = false; m.outgoing = true;
    m.channel = 0; m.toNum = toNum; m.fromNum = s_ownNodeNum;
    m.direct = true; m.at = millis();
    m.msgId = r.pid; m.pktId = r.pid; m.status = MsgStatus::PENDING;
    strncpy(m.from, "YOU", sizeof(m.from) - 1);
    memcpy(m.body, text, tlen); m.body[tlen] = '\0';
    if (xQueueSend(s_reqQ, &r, 0) != pdTRUE) {
        m.status = MsgStatus::FAILED; m.tDone = millis(); m.routeErr = 200;
        s_sendResult = SendResult::FAILED;
    }
    s_last = m;
    inboxPush(m);
    if (s_inboxQ) xQueueSendToFront(s_inboxQ, &m, 0);
    return m.status != MsgStatus::FAILED;
}

bool contactHasKey(uint32_t num) {
    Contact* c = findContact(num);
    return c && c->hasKey && c->keyLen > 0;
}

bool traceStart(uint32_t num) {
    if (s_state != State::READY || !s_reqQ || !num) return false;
    Req r; memset(&r, 0, sizeof r);
    r.kind = Req::TRACE; r.to = num;
    if (xQueueSend(s_reqQ, &r, 0) != pdTRUE) return false;
    // Marked in flight now; the task fills target/at on write (or clears on
    // refusal). The UI shows "sending" meanwhile.
    s_trace.active = true; s_trace.waiting = true;
    s_trace.target = num; s_trace.at = millis(); s_trace.n = 0;
    return true;
}
const TraceResult& traceResult() { return s_trace; }
void tracePoll() {
    if (s_trace.active && s_trace.waiting && (millis() - s_trace.at) > TRACE_TIMEOUT_MS)
        s_trace.waiting = false;   // timed out; active stays so the UI can say so
}

bool msgById(uint32_t pktId, Message& out) {
    Message* m = findByPid(pktId);
    if (!m) {
        // Incoming messages file by their air id, not pktId.
        for (uint8_t i = 0; i < s_inboxLen; i++) {
            const Message& c = inboxAt(i);
            if (!c.outgoing && c.msgId == pktId && pktId) { out = c; return true; }
        }
        return false;
    }
    out = *m;
    return true;
}

SendResult takeSendResult() {
    SendResult r = s_sendResult;
    s_sendResult = SendResult::NONE;
    return r;
}

uint8_t        contactCount() { return s_contactN; }
const Contact& contactAt(uint8_t i) {
    static Contact empty;
    return s_contacts[i < s_contactN ? i : 0];
}
bool contactName(uint32_t num, char* out, size_t cap) {
    if (!out || !cap) return false;
    out[0] = '\0';
    Contact* c = findContact(num);
    if (!c) return false;
    const char* s = c->shortName[0] ? c->shortName : (c->longName[0] ? c->longName : nullptr);
    if (!s) return false;
    strncpy(out, s, cap - 1);
    out[cap - 1] = '\0';
    return true;
}
void     setDmTarget(uint32_t num) { s_dmTarget = num; }
uint32_t dmTarget() { return s_dmTarget; }

bool popMessage(Message& out) {
    if (!s_inboxQ) return false;
    if (xQueueReceive(s_inboxQ, &out, 0) != pdTRUE) return false;
    // Keep it in the ring for the SQUAD-style list: push to a parallel array.
    return true;
}
const Message& lastMessage() { return s_last; }

uint8_t inboxCount() { return s_inboxLen; }
const Message& inboxAt(uint8_t i) {
    static Message empty;
    if (i >= s_inboxLen) return empty;
    int idx = (int)s_inboxHead - 1 - (int)i;
    while (idx < 0) idx += INBOX_N;
    return s_inbox[idx % INBOX_N];
}
uint8_t unreadCount() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_inboxLen; i++) if (inboxAt(i).unread) n++;
    return n;
}
void markInboxRead() {
    for (uint8_t i = 0; i < s_inboxLen; i++) s_inbox[i].unread = false;
}
// Per-conversation read/unread, for the dots in the channel/contact lists:
// a conversation is the open DM, else the send channel.
static bool msgInOpenChat(const Message& m) {
    if (s_dmTarget) return m.direct && (m.fromNum == s_dmTarget || m.toNum == s_dmTarget);
    return !m.direct && m.channel == s_sendChan;
}
bool chatMatches(const Message& m) { return msgInOpenChat(m); }
uint8_t unreadChannel(uint8_t idx) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_inboxLen; i++) {
        const Message& m = inboxAt(i);
        if (m.unread && !m.direct && m.channel == idx) n++;
    }
    return n;
}
uint8_t unreadDirect(uint32_t num) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_inboxLen; i++) {
        const Message& m = inboxAt(i);
        if (m.unread && m.direct && (m.fromNum == num || m.toNum == num)) n++;
    }
    return n;
}
void markChatRead() {
    for (uint8_t i = 0; i < s_inboxLen; i++) {
        int idx = (int)s_inboxHead - 1 - (int)i;
        while (idx < 0) idx += INBOX_N;
        Message& m = s_inbox[idx % INBOX_N];
        if (msgInOpenChat(m)) m.unread = false;
    }
}

void onAdvertised(const uint8_t mac[6], const char* name, int8_t rssi, uint8_t target, uint8_t addrType) {
    if (!s_scanning || !s_nodeMux) return;
    if (s_state != State::SCANNING) return;
    if (xSemaphoreTake(s_nodeMux, 0) != pdTRUE) return;
    for (uint8_t i = 0; i < s_nodeN; i++) {
        if (memcmp(s_nodes[i].mac, mac, 6) == 0) {
            s_nodes[i].rssi = rssi;
            s_nodes[i].addrType = addrType;
            if (name && name[0]) { strncpy(s_nodes[i].name, name, sizeof(s_nodes[i].name) - 1); }
            xSemaphoreGive(s_nodeMux);
            return;
        }
    }
    if (s_nodeN < NODE_MAX) {
        Node& n = s_nodes[s_nodeN++];
        memset(&n, 0, sizeof n);
        memcpy(n.mac, mac, 6);
        if (name && name[0]) strncpy(n.name, name, sizeof(n.name) - 1);
        n.rssi = rssi; n.target = target; n.addrType = addrType;
    }
    xSemaphoreGive(s_nodeMux);
}

const uint8_t* ownMac() { return (const uint8_t*)s_ownMac; }

} // namespace MeshLink
#endif // MESH_COMPANION
