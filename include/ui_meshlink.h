// SquachWatch-CYD — the companion screens, reached from the SquachMesh menu
// when MODE is COMPANION. See mesh_link.h for the link itself.
//
// A small section of its own: the node list (find and connect), the channels
// (pick which one a message goes to), the contacts (nodes heard; picking one
// addresses a direct message), and the conversation itself, with WRITE opening
// the same keyboard the payphone uses.
#pragma once
#if MESH_COMPANION
#include <TFT_eSPI.h>
#include <stdint.h>
#include "mesh_link.h"

class DetectionEngine;

void uiMeshNodesInit(TFT_eSPI& t);
void uiMeshNodesTick(TFT_eSPI& t, uint32_t now, bool advance = true);
enum class MeshNodesHit : uint8_t { ROW, SCAN, CONNECT, DISCONNECT, BACK, NONE };
MeshNodesHit uiMeshNodesHit(TFT_eSPI& t, int x, int y, int* row);
void uiMeshNodesSelect(int row);
int  uiMeshNodesSelected();
void uiMeshNodesScroll(int delta);

void uiMeshChannelsInit(TFT_eSPI& t);
void uiMeshChannelsTick(TFT_eSPI& t, uint32_t now, bool advance = true);
// ROW selects the channel (opens its chat); BELL toggles whether the
// channel's mail pops up on the main screen (toast + bubble). DMs always
// notify, channels are opt-in.
enum class MeshChannelsHit : uint8_t { ROW, BELL, BACK, NONE };
MeshChannelsHit uiMeshChannelsHit(TFT_eSPI& t, int x, int y, int* row);
void uiMeshChannelsScroll(int delta);

void uiMeshContactsInit(TFT_eSPI& t);
void uiMeshContactsTick(TFT_eSPI& t, uint32_t now, bool advance = true);
enum class MeshContactsHit : uint8_t { ROW, BACK, NONE };
MeshContactsHit uiMeshContactsHit(TFT_eSPI& t, int x, int y, int* row);
void uiMeshContactsScroll(int delta);

void uiMeshChatInit(TFT_eSPI& t);
void uiMeshChatTick(TFT_eSPI& t, uint32_t now, bool advance = true);
enum class MeshChatHit : uint8_t { CHANNEL, WRITE, BACK, NONE };
MeshChatHit uiMeshChatHit(TFT_eSPI& t, int x, int y);
void uiMeshChatScroll(int delta);
// Which message bubble sits under a tap (own or чужой -- both have an info
// screen). False when the tap hit no bubble.
bool uiMeshChatBubbleHit(TFT_eSPI& t, int x, int y, uint32_t* pktId);

// One message's delivery story: state, timeline, hops, traceroute.
void        uiMeshMsgInfoShow(uint32_t pktId);
// The DM peer of the shown message (0 for channels / unknown), for TRACE.
uint32_t    uiMeshMsgInfoPeer();
void        uiMeshMsgInfoInit(TFT_eSPI& t);
void        uiMeshMsgInfoTick(TFT_eSPI& t, uint32_t now, bool advance = true);
enum class MeshInfoHit : uint8_t { TRACE, BACK, NONE };
MeshInfoHit uiMeshMsgInfoHit(TFT_eSPI& t, int x, int y);

// Human words for a delivery state / routing error.
const char* meshStatusLabel(MeshLink::MsgStatus s);
const char* meshRouteErrLabel(uint8_t err);

// The SEND chooser: canned messages, and a MANUAL row that opens the keyboard.
// Index 0 is MANUAL; 1.. are templates. uiMeshTemplateAt() returns the text of
// row `i` (never null in range); uiMeshTemplateCount() is the row count.
void        uiMeshTemplatesInit(TFT_eSPI& t);
void        uiMeshTemplatesTick(TFT_eSPI& t, uint32_t now, bool advance = true);
enum class MeshTplHit : uint8_t { ROW, BACK, NONE };
MeshTplHit  uiMeshTemplatesHit(TFT_eSPI& t, int x, int y, int* row);
uint8_t     uiMeshTemplateCount();
const char* uiMeshTemplateAt(uint8_t i);
void        uiMeshTemplatesScroll(int delta);

#endif // MESH_COMPANION