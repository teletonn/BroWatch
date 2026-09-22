// SquachWatch-CYD — the companion screens, reached from the SquachMesh menu
// when MODE is COMPANION. See mesh_link.h for the link itself.
//
// Two screens, both plain row lists in the same shape as the rest of the
// sub-screens: the node list (find a LoRa node and connect to it) and the
// message log (read what arrived, pick a channel, write one).
#pragma once
#if MESH_COMPANION
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

void uiMeshNodesInit(TFT_eSPI& t);
void uiMeshNodesTick(TFT_eSPI& t, uint32_t now, bool advance = true);
// ROW fills *row with the node index.
enum class MeshNodesHit : uint8_t { ROW, SCAN, CONNECT, DISCONNECT, BACK, NONE };
MeshNodesHit uiMeshNodesHit(TFT_eSPI& t, int x, int y, int* row);

void uiMeshChatInit(TFT_eSPI& t);
void uiMeshChatTick(TFT_eSPI& t, uint32_t now, bool advance = true);
enum class MeshChatHit : uint8_t { CHANNEL, WRITE, BACK, NONE };
MeshChatHit uiMeshChatHit(TFT_eSPI& t, int x, int y);

#endif // MESH_COMPANION