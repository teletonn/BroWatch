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

// ---- channels ----
constexpr uint8_t CHAN_MAX = 8;
struct Channel {
    uint8_t index;            // the node's channel index (what goes in a packet)
    char    name[24];
    bool    present;
};
uint8_t        channelCount();
const Channel& channelAt(uint8_t i);
void           setSendChannel(uint8_t i);
uint8_t        sendChannel();

// ---- messages ----
constexpr uint8_t TEXT_MAX = 200;
struct Message {
    bool     have;
    bool     unread;
    bool     outgoing;        // true: we sent it (shown on the right)
    uint8_t  channel;
    uint8_t  mac[6];          // the node it came from / went to
    char     from[24];        // sender's short name, or "YOU"
    char     body[TEXT_MAX + 1];
    uint32_t at;              // millis() when it landed in our queue
};
bool           popMessage(Message& out);
const Message& lastMessage();
constexpr uint8_t INBOX_N = 12;
uint8_t        inboxCount();
const Message& inboxAt(uint8_t i);   // 0 is the newest

// Queue a channel text for the task to send. Returns false when not READY or
// the text is empty. The copy is bounded by TEXT_MAX.
bool sendChannelText(uint8_t channel, const char* text);

// Called from the shared BLE scan callback (host task). detection.cpp only
// calls this while COMPANION mode is on and the advert looks like a node.
void onAdvertised(const uint8_t mac[6], const char* name, int8_t rssi, uint8_t target, uint8_t addrType);

// This device's own address, as the node sees it. All zeros until begin().
const uint8_t* ownMac();

} // namespace MeshLink
#endif // MESH_COMPANION
