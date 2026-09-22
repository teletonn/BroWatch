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
    enum Kind : uint8_t { CONNECT, DISCONNECT, SEND, SCAN_ON, SCAN_OFF } kind;
    uint8_t  channel;
    uint32_t to;              // BROADCAST_TO for a channel message, else a node
    char     text[TEXT_MAX + 1];
};
QueueHandle_t s_reqQ = nullptr;
TaskHandle_t  s_task = nullptr;

// -------- FromNum doorbell: a monotonic count of FromRadio messages --------
volatile uint32_t s_fromNum   = 0;
volatile bool     s_fromNumNew= false;
uint32_t          s_lastRead  = 0;

NimBLEClient*              s_client = nullptr;
NimBLERemoteCharacteristic* s_toRadio = nullptr;
NimBLERemoteCharacteristic* s_fromRadio = nullptr;

uint32_t s_ownNodeNum = 0;
bool     s_haveComplete = false;

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

// ---- writer ----
struct Pbw { uint8_t* p; size_t n; size_t i; };
static void wVarint(Pbw& w, uint64_t v) {
    while (v >= 0x80 && w.i < w.n) { w.p[w.i++] = (uint8_t)(v | 0x80); v >>= 7; }
    if (w.i < w.n) w.p[w.i++] = (uint8_t)v;
}
static void wTag(Pbw& w, uint32_t field, uint32_t wire) { wVarint(w, ((uint64_t)field << 3) | wire); }
static void wVarintField(Pbw& w, uint32_t field, uint64_t v) { wTag(w, field, 0); wVarint(w, v); }
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
        else if (f == 8 && w == 2) { const uint8_t* d; size_t l; if (pbBytes(b, d, l)) c.hasKey = (l > 0); }
        else if (!pbSkip(b, w)) break;
    }
}
static void parseNodeInfo(Pb b) {
    uint32_t num = 0; uint32_t lastHeard = 0; Contact tmp;
    memset(&tmp, 0, sizeof tmp);
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 1 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; num = (uint32_t)v; }
        else if (f == 2 && w == 2) { Pb u; if (pbSub(b, u)) parseUser(u, tmp); }
        else if (f == 6 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; lastHeard = (uint32_t)v; }
        else if (!pbSkip(b, w)) break;
    }
    Contact* c = upsertContact(num);
    if (!c) return;
    if (tmp.shortName[0]) { strncpy(c->shortName, tmp.shortName, sizeof(c->shortName) - 1); c->shortName[sizeof(c->shortName) - 1] = '\0'; }
    if (tmp.longName[0])  { strncpy(c->longName,  tmp.longName,  sizeof(c->longName)  - 1); c->longName[sizeof(c->longName) - 1] = '\0'; }
    c->role = tmp.role;
    if (tmp.hasKey) c->hasKey = true;
    if (lastHeard) c->lastHeard = lastHeard;
}
// Channel: index=1, settings=2, role=3. ChannelSettings: channel_num=1,
// psk=2, name=3.
static void parseChannel(Pb b) {
    uint32_t index = 0, role = 0; char name[13] = ""; bool hasPsk = false;
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
                    else if (sf == 2 && sw == 2) { const uint8_t* d; size_t l; if (pbBytes(s, d, l)) hasPsk = (l > 0); }
                    else if (!pbSkip(s, sw)) break;
                }
            }
        }
        else if (!pbSkip(b, w)) break;
    }
    if (index < CHAN_MAX) {
        s_chans[index].index = (uint8_t)index;
        s_chans[index].role = (uint8_t)role;
        s_chans[index].hasPsk = hasPsk;
        s_chans[index].present = true;
        if (name[0]) { strncpy(s_chans[index].name, name, sizeof(s_chans[index].name) - 1); s_chans[index].name[sizeof(s_chans[index].name) - 1] = '\0'; }
        else if (role == 1) snprintf(s_chans[index].name, sizeof(s_chans[index].name), "PRIMARY");
        else if (role == 0) snprintf(s_chans[index].name, sizeof(s_chans[index].name), "OFF");
        else                snprintf(s_chans[index].name, sizeof(s_chans[index].name), "CH%u", (unsigned)index);
        if (index + 1 > s_chanN) s_chanN = (uint8_t)(index + 1);
    }
}
static void pushIncoming(uint32_t fromNum, uint32_t toNum, uint8_t channel, const uint8_t* text, size_t len) {
    Message m;
    memset(&m, 0, sizeof m);
    m.have = true; m.unread = true; m.outgoing = false;
    m.channel = channel;
    m.fromNum = fromNum;
    m.toNum = toNum;
    m.direct = (toNum != 0xFFFFFFFFu && toNum != 0);
    m.at = millis();
    char nb[8];
    strncpy(m.from, nameFor(fromNum, nb, sizeof nb), sizeof(m.from) - 1);
    size_t c = len < TEXT_MAX ? len : TEXT_MAX;
    memcpy(m.body, text, c); m.body[c] = '\0';
    s_last = m;
    inboxPush(m);
    if (s_inboxQ) xQueueSendToFront(s_inboxQ, &m, 0);   // newest first
    Serial.printf("[meshlink] RX %s ch%u from %s: %s\n", m.direct ? "DM" : "ch", (unsigned)channel, m.from, m.body);
}
static void parseMeshPacket(Pb b) {
    uint32_t fromNum = 0, toNum = 0; uint8_t channel = 0;
    uint32_t portnum = 0; const uint8_t* payload = nullptr; size_t plen = 0;
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 1 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; fromNum = (uint32_t)v; }
        else if (f == 2 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; toNum = (uint32_t)v; }
        else if (f == 3 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; channel = (uint8_t)v; }
        else if (f == 4 && w == 2) {  // Data
            Pb d;
            if (pbSub(b, d)) {
                while (d.ok && d.i < d.n) {
                    uint32_t df, dw;
                    if (!pbTag(d, df, dw)) break;
                    if (df == 1 && dw == 0) { uint64_t v; if (!pbVarint(d, v)) break; portnum = (uint32_t)v; }
                    else if (df == 2 && dw == 2) { if (!pbBytes(d, payload, plen)) break; }
                    else if (!pbSkip(d, dw)) break;
                }
            }
        }
        else if (!pbSkip(b, w)) break;
    }
    if (portnum == 1 /* TEXT_MESSAGE_APP */ && payload && plen)
        pushIncoming(fromNum, toNum, channel, payload, plen);
}
static void parseFromRadio(const uint8_t* d, size_t n) {
    Pb b{ d, n, 0, true };
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 2 && w == 2) { Pb p; if (pbSub(b, p)) parseMeshPacket(p); }
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
    }
}

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

bool sendChannelTextNow(uint32_t toNum, uint8_t channel, const char* text) {
    if (!s_toRadio || !text || !text[0]) return false;
    // ToRadio{ packet: MeshPacket{ to, channel, decoded: Data{ portnum=1, payload }, id, want_ack } }
    uint8_t data[8 + TEXT_MAX]; Pbw d{ data, sizeof data, 0 };
    wVarintField(d, 1, 1);                     // Data.portnum = TEXT_MESSAGE_APP
    wBytesField(d, 2, (const uint8_t*)text, strlen(text));  // Data.payload

    uint8_t pkt[16 + sizeof data]; Pbw p{ pkt, sizeof pkt, 0 };
    wVarintField(p, 2, toNum);                 // MeshPacket.to (broadcast or a node)
    wVarintField(p, 3, channel);               // MeshPacket.channel
    wBytesField(p, 4, data, d.i);              // MeshPacket.decoded
    wVarintField(p, 6, esp_random());          // MeshPacket.id
    wVarintField(p, 9, 3);                     // MeshPacket.hop_limit
    wVarintField(p, 10, 1);                    // MeshPacket.want_ack

    uint8_t out[8 + sizeof pkt]; Pbw o{ out, sizeof out, 0 };
    wBytesField(o, 1, pkt, p.i);               // ToRadio.packet
    bool ok = s_toRadio->writeValue(out, o.i, true);
    if (ok) {
        Message m; memset(&m, 0, sizeof m);
        m.have = true; m.unread = false; m.outgoing = true;
        m.channel = channel; m.toNum = toNum; m.fromNum = s_ownNodeNum;
        m.direct = (toNum != BROADCAST_TO && toNum != 0);
        m.at = millis();
        strncpy(m.from, "YOU", sizeof(m.from) - 1);
        strncpy(m.body, text, TEXT_MAX);
        s_last = m;
        inboxPush(m);
        if (s_inboxQ) xQueueSendToFront(s_inboxQ, &m, 0);
    }
    return ok;
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
    // Pause it for the attempt and bring it back either way -- the scan
    // callbacks live on the singleton, so a stop/start keeps detection whole.
    NimBLEScan* sc = NimBLEDevice::getScan();
    const bool wasScanning = sc && sc->isScanning();
    if (wasScanning) sc->stop();

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
        if (wasScanning) sc->start(0, false, false);
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
    if (s_authStatus < 0) {
        if (wasScanning) sc->start(0, false, false);
        s_client->disconnect();
        s_state = State::ERROR;
        snprintf(s_reason, sizeof s_reason, "pair failed");
        return;
    }

    if (wasScanning) sc->start(0, false, false);
    Serial.println("[meshlink] discovering GATT");
    NimBLERemoteService* svc = s_client->getService(NimBLEUUID(kMeshSvc));
    if (!svc) { snprintf(s_reason, sizeof s_reason, "no mesh service"); s_client->disconnect(); s_state = State::ERROR; return; }
    s_toRadio   = svc->getCharacteristic(NimBLEUUID(kToRadio));
    s_fromRadio = svc->getCharacteristic(NimBLEUUID(kFromRadio));
    NimBLERemoteCharacteristic* fromNum = svc->getCharacteristic(NimBLEUUID(kFromNum));
    Serial.printf("[meshlink] chars: to=%p from=%p num=%p\n", (void*)s_toRadio, (void*)s_fromRadio, (void*)fromNum);
    if (!s_toRadio || !s_fromRadio) { snprintf(s_reason, sizeof s_reason, "no chars"); s_client->disconnect(); s_state = State::ERROR; return; }

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
    s_state = s_scanning ? State::SCANNING : State::OFF;
}

void taskLoop(void*) {
    Req r;
    for (;;) {
        while (s_reqQ && xQueueReceive(s_reqQ, &r, 0) == pdTRUE) {
            switch (r.kind) {
                case Req::CONNECT:    doConnect(r.channel); break;
                case Req::DISCONNECT: doDisconnect(); break;
                case Req::SEND:       sendChannelTextNow(r.to, r.channel, r.text); break;
                case Req::SCAN_ON:    s_scanning = true;  if (s_state == State::OFF) s_state = State::SCANNING; break;
                case Req::SCAN_OFF:   s_scanning = false; if (s_state == State::SCANNING) s_state = State::OFF; break;
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
            if (s_fromNumNew) {
                s_fromNumNew = false;
                uint32_t diff = s_fromNum - s_lastRead;
                if (diff > 8) diff = 8;          // bound the work per tick
                for (uint32_t i = 0; i < diff; i++) {
                    if (!readOneFromRadio()) break;
                    s_lastRead++;
                }
            }
            if (s_client && !s_client->isConnected()) {
                s_reason[0] = '\0';
                snprintf(s_reason, sizeof s_reason, "lost node");
                s_state = State::ERROR;
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
}

void setTarget(Target t) { s_target = t; }
Target target()          { return s_target; }

void startScan() {
    s_scanning = true;
    s_scanSince = millis();
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

bool sendChannelText(uint8_t channel, const char* text) {
    if (s_state != State::READY || !text || !text[0] || !s_reqQ) return false;
    Req r; memset(&r, 0, sizeof r);
    r.kind = Req::SEND; r.channel = channel; r.to = BROADCAST_TO;
    strncpy(r.text, text, TEXT_MAX);
    return xQueueSend(s_reqQ, &r, 0) == pdTRUE;
}

bool sendDirectText(uint32_t toNum, const char* text) {
    if (s_state != State::READY || !text || !text[0] || !s_reqQ || !toNum) return false;
    Req r; memset(&r, 0, sizeof r);
    r.kind = Req::SEND; r.channel = 0; r.to = toNum;   // DMs go on the primary channel
    strncpy(r.text, text, TEXT_MAX);
    return xQueueSend(s_reqQ, &r, 0) == pdTRUE;
}

uint8_t        contactCount() { return s_contactN; }
const Contact& contactAt(uint8_t i) {
    static Contact empty;
    return s_contacts[i < s_contactN ? i : 0];
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
