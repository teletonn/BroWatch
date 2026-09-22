// BroWatch — USB bridge to browatch-web (and, later, the native app).
//
// A USB-connected board speaks JSON lines on its serial port:
//
//   [BW {"t":"peer","mac":"AA:BB:CC:DD:EE:FF","name":"Squachy","client":"bw cyd-ili9341/v1.x"}]
//   (the board's own announce also carries "nick": the owner's persona —
//   payphone NAME, else the indexed nickname. The web app speaks ONLY as
//   this persona and never invents its own.)
//   [BW {"t":"msg","from":"Squachy","text":"..."}]
//   [BW {"t":"emote","from":"AA:BB:...","emote":"WAVE"}]
//   [BW {"t":"detection","type":"FLOCK","mac":"...","rssi":-70,"vendor":"..."}]
//
// gateway.py forwards these to server.py's /api/ingest, so the web app shows
// the same chat, squad and detections as the board. Only the four frame kinds
// above are ever emitted: anything else would land in the web chat as junk.
//
// The other direction (web -> board -> mesh radio):
//
//   [BW {"t":"send","text":"..."}]     typed message, via MeshTalk::sendText
//   [BW {"t":"send","canned":12}]      canned line 12, via MeshTalk::send
//   [BW {"t":"emote","emote":2}]       emote 2 (MeshMsg::Emote), setup rolled here
//   [BW {"t":"hello"}]  a listener came up: re-announce + FULL snapshot
//                        (squad, inbox backlog, current detection)
//   [BW {"t":"ping"}]   keepalive: re-announce + squad snapshot ONLY, never
//                        rewinds the inbox/detection/emote cursors
//
// Compiled in only with -DBW_BRIDGE=1 (see platformio.ini). The radio path is
// untouched: this only reads state and queues mesh sends like a screen would.
#pragma once
#include <stdint.h>

class DetectionEngine;

namespace BwBridge {

// Once from setup(), after MeshTalk::begin(). Cheap: just clears state, the
// first announce waits until the radio hands over our own MAC (see tick()).
void begin();

// Every loop() pass, after engine.loop(). Emits new inbox messages,
// detections and emotes as they arrive, plus a squad snapshot every few
// seconds. Best effort: never blocks, never allocates.
void tick(uint32_t now, const DetectionEngine& eng);

// One full serial line starting with "[BW", handed over by Clock::pollSerial
// (the single serial reader). Not called when the board is locked.
void onLine(const char* line);

}  // namespace BwBridge
