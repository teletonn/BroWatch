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

// -------- node-number -> short name, for showing who a DM is from --------
constexpr uint8_t NODENAME_MAX = 16;
struct NodeName { uint32_t num; char shortName[16]; };
NodeName s_names[NODENAME_MAX];
uint8_t  s_nameN = 0;

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
    uint8_t channel;
    char    text[TEXT_MAX + 1];
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
//  name table
// =====================================================================
static void noteNodeName(uint32_t num, const char* shortName) {
    if (!num || !shortName || !shortName[0]) return;
    for (uint8_t i = 0; i < s_nameN; i++) {
        if (s_names[i].num == num) {
            strncpy(s_names[i].shortName, shortName, sizeof(s_names[i].shortName) - 1);
            s_names[i].shortName[sizeof(s_names[i].shortName) - 1] = '\0';
            return;
        }
    }
    if (s_nameN >= NODENAME_MAX) return;
    s_names[s_nameN].num = num;
    strncpy(s_names[s_nameN].shortName, shortName, sizeof(s_names[s_nameN].shortName) - 1);
    s_names[s_nameN].shortName[sizeof(s_names[s_nameN].shortName) - 1] = '\0';
    s_nameN++;
}
static const char* nameFor(uint32_t num, char* buf, size_t cap) {
    for (uint8_t i = 0; i < s_nameN; i++)
        if (s_names[i].num == num) return s_names[i].shortName;
    snprintf(buf, cap, "NODE %04X", (unsigned)(num & 0xFFFF));
    return buf;
}

// =====================================================================
//  parsing a FromRadio
// =====================================================================
static void parseUser(Pb b, char* shortOut, size_t cap) {
    shortOut[0] = '\0';
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 3 && w == 2) { const uint8_t* d; size_t l; if (pbBytes(b, d, l)) { size_t c = l < cap - 1 ? l : cap - 1; memcpy(shortOut, d, c); shortOut[c] = '\0'; } }
        else if (!pbSkip(b, w)) break;
    }
}
static void parseNodeInfo(Pb b) {
    uint32_t num = 0; char shortName[16] = "";
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 1 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; num = (uint32_t)v; }
        else if (f == 2 && w == 2) { Pb u; if (pbSub(b, u)) parseUser(u, shortName, sizeof shortName); }
        else if (!pbSkip(b, w)) break;
    }
    noteNodeName(num, shortName);
}
static void parseChannel(Pb b) {
    uint32_t index = 0; char name[24] = "";
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 1 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; index = (uint32_t)v; }
        else if (f == 2 && w == 2) {  // ChannelSettings
            Pb s;
            if (pbSub(b, s)) {
                while (s.ok && s.i < s.n) {
                    uint32_t sf, sw;
                    if (!pbTag(s, sf, sw)) break;
                    if (sf == 3 && sw == 2) { const uint8_t* d; size_t l; if (pbBytes(s, d, l)) { size_t c = l < sizeof(name) - 1 ? l : sizeof(name) - 1; memcpy(name, d, c); name[c] = '\0'; } }
                    else if (!pbSkip(s, sw)) break;
                }
            }
        }
        else if (!pbSkip(b, w)) break;
    }
    if (index < CHAN_MAX) {
        s_chans[index].index = (uint8_t)index;
        s_chans[index].present = true;
        if (name[0]) strncpy(s_chans[index].name, name, sizeof(s_chans[index].name) - 1);
        else snprintf(s_chans[index].name, sizeof(s_chans[index].name), "CH %u", (unsigned)index);
        if (index + 1 > s_chanN) s_chanN = (uint8_t)(index + 1);
    }
}
static void pushIncoming(uint32_t fromNum, uint8_t channel, const uint8_t* text, size_t len) {
    Message m;
    memset(&m, 0, sizeof m);
    m.have = true; m.unread = true; m.outgoing = false;
    m.channel = channel;
    m.at = millis();
    char nb[24];
    strncpy(m.from, nameFor(fromNum, nb, sizeof nb), sizeof(m.from) - 1);
    size_t c = len < TEXT_MAX ? len : TEXT_MAX;
    memcpy(m.body, text, c); m.body[c] = '\0';
    s_last = m;
    inboxPush(m);
    if (s_inboxQ) xQueueSendToFront(s_inboxQ, &m, 0);   // newest first
    Serial.printf("[meshlink] RX ch%u from %s: %s\n", (unsigned)channel, m.from, m.body);
}
static void parseMeshPacket(Pb b) {
    uint32_t fromNum = 0; uint8_t channel = 0;
    uint32_t portnum = 0; const uint8_t* payload = nullptr; size_t plen = 0;
    while (b.ok && b.i < b.n) {
        uint32_t f, w;
        if (!pbTag(b, f, w)) break;
        if (f == 1 && w == 0) { uint64_t v; if (!pbVarint(b, v)) break; fromNum = (uint32_t)v; }
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
        pushIncoming(fromNum, channel, payload, plen);
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

bool sendChannelTextNow(uint8_t channel, const char* text) {
    if (!s_toRadio || !text || !text[0]) return false;
    // ToRadio{ packet: MeshPacket{ to: broadcast, channel, decoded: Data{ portnum=1, payload }, id, want_ack } }
    uint8_t data[8 + TEXT_MAX]; Pbw d{ data, sizeof data, 0 };
    wVarintField(d, 1, 1);                     // Data.portnum = TEXT_MESSAGE_APP
    wBytesField(d, 2, (const uint8_t*)text, strlen(text));  // Data.payload

    uint8_t pkt[16 + sizeof data]; Pbw p{ pkt, sizeof pkt, 0 };
    wVarintField(p, 2, BROADCAST_TO);          // MeshPacket.to
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
        m.have = true; m.unread = false; m.outgoing = true; m.channel = channel;
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
                case Req::SEND:       sendChannelTextNow(r.channel, r.text); break;
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
                Serial.printf("[meshlink] READY: own=%08X, %u channels\n", (unsigned)s_ownNodeNum, (unsigned)s_chanN);
                for (uint8_t ci = 0; ci < s_chanN; ci++)
                    Serial.printf("[meshlink]   ch%u = %s\n", (unsigned)s_chans[ci].index, s_chans[ci].name);
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
#if MESH_COMPANION_AUTOSTART
        if (s_state == State::SCANNING && s_nodeN > 0) {
            Serial.printf("[meshlink] AUTOSTART: node %02X:%02X:%02X:%02X:%02X:%02X found, connecting\n",
                          s_nodes[0].mac[0], s_nodes[0].mac[1], s_nodes[0].mac[2],
                          s_nodes[0].mac[3], s_nodes[0].mac[4], s_nodes[0].mac[5]);
            doConnect(0);
        } else if (s_state == State::ERROR && millis() - s_stateAt > 3000) {
            // Bench build: keep trying, so a fix can be observed without a
            // reflash between attempts.
            Serial.println("[meshlink] AUTOSTART: retrying");
            startScan();
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
    memset(s_names, 0, sizeof s_names);
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
    if (s_reqQ) { Req r; memset(&r, 0, sizeof r); r.kind = Req::DISCONNECT; xQueueSend(s_reqQ, &r, 0); }
    s_scanning = false;
}

void setTarget(Target t) { s_target = t; }
Target target()          { return s_target; }

void startScan() {
    s_scanning = true;
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
    if (!s_reqQ) return;
    Req r; memset(&r, 0, sizeof r); r.kind = Req::DISCONNECT; xQueueSend(s_reqQ, &r, 0);
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
    r.kind = Req::SEND; r.channel = channel;
    strncpy(r.text, text, TEXT_MAX);
    return xQueueSend(s_reqQ, &r, 0) == pdTRUE;
}

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
