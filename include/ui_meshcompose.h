// SquachWatch-CYD — the SquachMesh message screen: the last message received,
// and something to say back -- a ready-made line, or your own typed on the
// payphone. Opened from the speech bubble on the main screen.
#pragma once
#if SQUACH_MESH
#include <TFT_eSPI.h>
#include <stdint.h>

class DetectionEngine;

// HELP: the "?" -- replay the messages tutorial (see meshtutor.h).
// TYPE: open the keyboard with uiMeshComposeTyped() -- TYPE, or EDIT on a
// typed message waiting to be sent.
enum class ComposeHit : uint8_t { NONE, SENT, BACK, HELP, TYPE };

void       uiMeshComposeInit(TFT_eSPI& t);
// `advance` is false on the second of the 3.5"'s two band passes -- the
// same frame drawn again -- so anything that steps by the call rather
// than by the clock must sit still for it. Other boards draw once.
void       uiMeshComposeTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng, bool advance = true);
ComposeHit uiMeshComposeTouch(int x, int y, uint32_t now);

// Back from the keyboard with a message: show it large, with EDIT and SEND.
void        uiMeshComposeSetTyped(const char* text);
// What the keyboard should start from ("" for a new message).
const char* uiMeshComposeTyped();

// A reaction to the message being read: LIKE or DISLIKE as an ordinary
// canned message (CANNED_REACT_*), sent from the incoming bubble's own
// buttons on CLEAR and on the desk. True when it went out. Feedback is a
// toast -- the bubble and the letter have no status line the way this
// screen does -- and a sent reaction counts as read.
bool uiMessageSendReaction(uint8_t canned, uint32_t now);
#endif
