// SquachWatch-CYD — the companion link: this device as the *client* of an
// external LoRa node (a Meshtastic radio), instead of the SquachMesh peer it
// is in BROMESH mode.
//
// The two are mutually exclusive ways to use the same radio. SquachMesh
// (mesh.cpp/meshtalk.cpp) is our own BLE protocol, spoken peer to peer. This
// is somebody else's: a real LoRa node -- the LilyGo T3-S3 on the bench --
// runs Meshtastic, and this device connects to it over BLE exactly as the
// phone app would, and sends and receives its text messages.
//
// THREADING. Unlike SquachMesh, the BLE work here is not a scan callback and
// a loop-tick; it is a GATT client whose reads block until the node has
// something. So the client lives on its own task (meshLinkTask), and:
//   - onAdvertised() runs on the BLE host task (the shared scan callback in
//     detection.cpp hands every advert here while COMPANION mode is on);
//   - send requests come from the loop task through a queue;
//   - the state the screens read is a handful of volatile scalars.
// Nothing here touches SquachMesh's state, and BROMESH mode never calls into
// this file at all.
#pragma once
#if MESH_COMPANION
#include <stdint.h>
#include <stddef.h>

// A bench-only build flag: with COMPANION_AUTOSTART the link starts scanning
// and connects itself to the first node it sees, with no screen taps. Used to
// exercise the BLE central + Meshtastic handshake on real hardware; undefined
// (0) everywhere else.
#if !defined(MESH_COMPANION_AUTOSTART)
#define MESH_COMPANION_AUTOSTART 0
#endif

namespace MeshLink {

// Which external protocol the node is expected to speak. Only Meshtastic is
// implemented so far; MeshCore is the second backend (its wire format is a
// plain byte protocol, no protobuf) and is stubbed.
enum class Target : uint8_t { MESHTASTIC = 0, MESHCORE = 1 };
const char* targetLabel(Target t);

// OFF        : not started / disconnected by the user
// SCANNING   : looking for nodes (adverts being collected)
// CONNECTING : a connect is in flight
// HANDSHAKE  : connected, fetching config and channels
// READY      : connected and exchanging messages
// ERROR      : the last attempt failed; stateLabel() says why
enum class State : uint8_t { OFF, SCANNING, CONNECTING, HANDSHAKE, READY, ERROR };

void begin();
void tick(uint32_t now);
// Tear the link down (disconnect + stop). Called when leaving COMPANION mode.
void shutdown();

void   setTarget(Target t);
Target target();

// ---- discovery ----
constexpr uint8_t NODE_MAX = 12;
void    startScan();
void    stopScan();
uint8_t nodeCount();
// `name` may be empty; the address is always there.
struct Node {
    uint8_t mac[6];
    char    name[24];
    int8_t  rssi;
    uint8_t target;   // which protocol it looked like (Target)
    uint8_t addrType; // BLE address type (public/random) -- a node advertises
                      // a random-static address and connecting to it as public
                      // is refused, so this has to travel with the address.
};
const Node& nodeAt(uint8_t i);

// ---- connection ----
void  connect(uint8_t index);
void  disconnect();
State state();
const char* stateLabel();     // short, for the status row
bool  connected();

// ---- the bound node (auto-reconnect) ----
// A node the board should keep talking to with no screen taps: remembered in
// NVS by main.cpp, handed back here at boot (or when COMPANION is switched
// on), and then hunted for, connected to, and reconnected to on loss until
// something stops it. `disconnect()`/`shutdown()` clear the intent -- a user
// who drops the link is not reconnected behind their back.
void autoConnect(const uint8_t mac[6], uint8_t addrType);
// Same intent, but with nothing bound yet: scan, and if exactly one node is
// in range after a settling delay, bind and connect to it. Two or more nodes
// and it keeps scanning -- picking between them is the user's call.
void autoStart();
bool autoActive();
// The address of the node the link is (or was last) connected to, so a caller
// can bind it without having picked it from the node list itself. False when
// nothing has been linked yet.
bool linkedMac(uint8_t mac[6], uint8_t* addrType = nullptr);

// ---- channels ----
constexpr uint8_t CHAN_MAX = 8;
struct Channel {
    uint8_t index;            // the node's channel index (what goes in a packet)
    char    name[13];         // ChannelSettings.name; empty is the "Default" one
    uint8_t role;             // 0 DISABLED, 1 PRIMARY, 2 SECONDARY
    bool    hasPsk;
    uint8_t psk[32];          // the pre-shared key itself, for the chat's sake
    uint8_t pskLen;           // 0, 1 (simple/default), 16 or 32
    bool    present;
};
uint8_t        channelCount();
const Channel& channelAt(uint8_t i);
void           setSendChannel(uint8_t i);
uint8_t        sendChannel();
// Per-channel main-screen notification (toast + herald bubble). DMs always
// notify; a channel only when its bell is on (primary by default).
bool           channelNotify(uint8_t idx);
void           setChannelNotify(uint8_t idx, bool on);

// ---- contacts ----
// Nodes the radio has heard (Meshtastic NodeInfo). Read from the config dump
// and from live NODEINFO_APP broadcasts.
constexpr uint8_t CONTACT_MAX = 32;
struct Contact {
    uint32_t num;             // the node number, which is what a DM is sent to
    char     shortName[6];    // "KD7ABC" style, 4 chars on the mesh
    char     longName[25];
    uint8_t  role;
    bool     hasKey;          // has a public key, so a DM can be encrypted
    uint8_t  key[32];         // the public key itself, kept for the chat's sake
    uint8_t  keyLen;          // 0, or 32 for a Meshtastic X25519 key
    uint32_t lastHeard;
    bool     used;
};
uint8_t        contactCount();
const Contact& contactAt(uint8_t i);
// Copy a contact's short (fallback long) name. False when unknown.
bool           contactName(uint32_t num, char* out, size_t cap);
// A DM target (0 = none). sendChannelText to the target's channel with
// `to` set to the contact is a direct message.
void     setDmTarget(uint32_t num);
uint32_t dmTarget();

// ---- messages ----
// The network's own ceiling on a text message, in BYTES of UTF-8: Meshtastic
// caps a text at 200 (its clients show that count), and a Russian letter is
// two of them. The compose screen counts bytes against this, so the number it
// shows is the number the radio will actually accept.
constexpr uint16_t TEXT_LIMIT = 200;
constexpr uint8_t TEXT_MAX = 200;
constexpr uint8_t MSG_MAX  = 24;
// Delivery states of an outgoing message, in order. Broadcasts never get a
// Routing ack, so IN_MESH (the node took it) is their terminal state;
// DMs continue to DELIVERED (Routing NONE) or FAILED. Times are millis()
// stamps, 0 when the step has not happened yet.
enum class MsgStatus : uint8_t { PENDING, SENT, QUEUED, IN_MESH, DELIVERED, FAILED };
struct Message {
    bool     have;
    bool     unread;
    bool     outgoing;        // true: we sent it (shown on the right)
    bool     direct;          // a DM, not a channel broadcast
    uint8_t  channel;
    uint32_t fromNum;         // sender node number (0 for us)
    uint32_t toNum;           // destination node number (broadcast all-ones)
    uint8_t  mac[6];          // the node it came from / went to
    char     from[8];         // sender's short name, or YOU
    char     body[TEXT_MAX + 1];
    uint32_t at;              // millis() when it landed in our queue
    // Outgoing delivery tracking.
    uint32_t  pktId;          // our MeshPacket.id, links QueueStatus/Routing
    uint32_t  msgId;          // air id: ours (== pktId) or the sender's packet
                              // id for incoming (what a reply/reaction names)
    MsgStatus status;         // PENDING until written to the node
    uint32_t  tSent;          // written to ToRadio
    uint32_t  tQueued;        // node accepted (QueueStatus res=0)
    uint32_t  tDone;          // DELIVERED / IN_MESH / FAILED moment
    uint8_t   routeErr;       // Routing error_reason, or QueueStatus res
    // Incoming path info.
    uint8_t   hops;           // hop_start - hop_limit, 0 when direct/unknown
    uint32_t  relayNode;      // last relay, 0 when none/direct
};
bool           popMessage(Message& out);
const Message& lastMessage();
constexpr uint8_t INBOX_N = MSG_MAX;
uint8_t        inboxCount();
const Message& inboxAt(uint8_t i);   // 0 is the newest
// How many inbox messages are still unread, for the main screen's badge.
uint8_t        unreadCount();
// Unread in one channel / one DM thread, for the list dots.
uint8_t        unreadChannel(uint8_t idx);
uint8_t        unreadDirect(uint32_t num);
// Mark everything read. Called when a conversation is opened.
void           markInboxRead();
// Mark the open conversation (DM target or send channel) read.
void           markChatRead();
// True when a message belongs to the open conversation (same rule the chat
// list draws by).
bool           chatMatches(const Message& m);
// A copy of the inbox message with this packet id (for the info screen).
bool           msgById(uint32_t pktId, Message& out);

// ---- traceroute ----
// Ask the mesh how our packets reach `num`: an empty RouteDiscovery on
// TRACEROUTE_APP, the way the official clients do it. The reply lands in
// traceResult(); poll it (timeout 30 s) from the UI tick.
bool           traceStart(uint32_t num);
struct TraceResult {
    bool     active;          // a trace is in flight or finished
    bool     waiting;         // in flight, no reply yet
    bool     replied;         // a reply was parsed (path may still be empty)
    uint32_t target;
    uint32_t at;              // millis() when sent
    uint8_t  n;               // hops in route[]
    uint32_t route[8];        // node numbers along the path
    int8_t   snr[8];          // SNR towards each hop, dB
};
const TraceResult& traceResult();
void               tracePoll();

// Queue a channel text for the task to send. Returns false when not READY or
// the text is empty. The copy is bounded by TEXT_MAX. Texts go out paced
// (see SEND_PACE_MS): the task holds them so the node never sees two texts
// inside its 2-second rate window.
bool sendChannelText(uint8_t channel, const char* text, uint32_t replyId = 0);
// A direct message to a contact: the same packet with `to` set to the node.
// Refused (false) when the contact's public key is unknown -- without it the
// node cannot PKI-encrypt and fails the send.
bool sendDirectText(uint32_t toNum, const char* text, uint32_t replyId = 0);
// Whether the node DB gave us this contact's public key (a DM prerequisite).
bool contactHasKey(uint32_t num);
// Minimum gap between two text writes, in ms. The node drops a second text
// inside 2 s with RATE_LIMIT_EXCEEDED; we stay well clear of it.
constexpr uint32_t SEND_PACE_MS = 2200;
// The outcome of the last send, for the UI. Written by the task when a
// QueueStatus or Routing reply names one of our packet ids; the loop takes it
// (clearing to NONE) and toasts failures.
enum class SendResult : uint8_t { NONE, QUEUED, SENT, RATE_LIMITED, PKI_FAILED, NO_KEY, NOT_READY, FAILED };
SendResult takeSendResult();

// Called from the shared BLE scan callback (host task). detection.cpp only
// calls this while COMPANION mode is on and the advert looks like a node.
void onAdvertised(const uint8_t mac[6], const char* name, int8_t rssi, uint8_t target, uint8_t addrType);

// This device's own address, as the node sees it. All zeros until begin().
const uint8_t* ownMac();

} // namespace MeshLink
#endif // MESH_COMPANION
