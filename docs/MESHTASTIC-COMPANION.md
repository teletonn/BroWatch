# Meshtastic companion: protocol notes, lessons, and the send path

This is the working reference for the BroWatch companion mode (BLE "phone"
client to a Meshtastic node via the PhoneAPI). Everything below was verified
against the reference firmware in `../lora-mesh/firmware/src` and against the
live T3-S3 bench node (own num `DA5ACDE0`, 8 channels, ~26 contacts).

## 1. BLE service and characteristics

Meshtastic service `6ba1b218-15a8-461f-9fa8-5dcae273eafd`:

| Char | UUID | Phone use |
|---|---|---|
| ToRadio | `f75c76d2-129e-4dad-a1dd-7866124401e7` | WRITE (with response). Phone → node. |
| FromRadio | `2c55e69e-4993-11ed-b878-0242ac120002` | READ. Node → phone. Drain on notify + 400 ms poll. |
| FromNum | `ed9da18c-a800-4f66-a670-aa7547e34453` | NOTIFY+READ, uint32 LE counter. Doorbell. |
| LogRadio | `5a3d6e49-06e6-4423-9944-e9de8cdf9547` | NOTIFY. Node firmware log (diagnostic only). |

Set a large ATT MTU (`NimBLEDevice::setMTU(517)`); text ToRadio writes are
~40 bytes and must not be squeezed through MTU 23.

Handshake: write `ToRadio.want_config_id` (field 3, we use `0x50C0FFEE`),
then read FromRadio until `config_complete_id` (FromRadio field 7).
Expected dump order: my_info(3), deviceuiConfig(17), own node_info(4),
metadata(13), channels xN(10), config(5), moduleConfig(9), node_infos(4),
fileInfo(15), config_complete(7).

## 2. FromRadio field numbers (from `mesh.pb.h`)

`packet=2, my_info=3, node_info=4, config=5, log_record=6,
config_complete_id=7, rebooted=8, moduleConfig=9, channel=10,
queueStatus=11, xmodemPacket=12, metadata=13, mqttClientProxy=14,
fileInfo=15, clientNotification=16, deviceuiConfig=17, lockdown_status=18,
region_presets=19`.

## 3. What the phone must send for a text

`ToRadio.packet` (field 1) → `MeshPacket`:

- `from=1` — **our node num, encoded fixed32 (wire 5). Always set it.** The
  firmware zeroes it before routing, but self-addressed routing NAKs/acks
  (rate limit, PKI failure, encode errors) are addressed to the
  phone-supplied `from`; with `from=0` they go to `to=0` and never reach us.
  Without `from` our text writes were silently dropped by the node
  (no QueueStatus, no routing reply at all).
- `to=2` — `0xFFFFFFFF` for a channel broadcast, else the destination node
  num, **fixed32**.
- `channel=3` — the channel **index** (0-based), varint. DMs use 0; the
  firmware substitutes the channel it last heard the destination on
  (`getEffectiveChannelIndex`), or keeps 0.
- `decoded=4` — `Data{ portnum=1 (TEXT_MESSAGE_APP), payload }`, ≤200 bytes
  (`meshtastic_Constants_DATA_PAYLOAD_LEN` is 233; we use 200, UTF-8 safe).
- `id=6` — random nonzero uint32, **fixed32**. Used for the flood dedup, the
  PKI nonce, and to match QueueStatus/Routing replies. `esp_random()`,
  never 0.
- `hop_limit=9` — 3 (varint). `want_ack=10` — 1 for DMs only (varint bool);
  nobody acks a broadcast, and the firmware clears it there anyway.
- **Wire-format gotcha (2025+, firmware ≥2.5): `MeshPacket.from/to/id`,
  `Data.request_id/reply_id/dest/source`, `NodeInfo.last_heard` are
  fixed32 (wire 5), NOT varints.** Verified against the official Python
  client's `mesh_pb2` descriptors and live packets from STVG
  (`0D 5C0A304A ...` = from as fixed32 LE). A varint-encoded `from/to/id`
  is skipped by the node's decoder, so the node sees `from=0/to=0`: our
  sends were dropped/misrouted and every incoming parsed as
  `from=0000/to=broadcast` (DMs misfiled as channel mail from "0000").
  Decode must accept both wires (old firmware sent varints); encode fixed32.
- **Do NOT set `pki_encrypted` or `public_key`.** The firmware decides PKI vs
  channel on its own. Setting `pki_encrypted=true` when PKI is impossible
  forces a hard failure instead of a fallback.

## 4. How the node handles our packet (Router/MeshService)

- Phone packets are **never echoed** to the phone (`loopback=false`, and
  `handleFromRadio` drops `isFromUs && !isToUs`). The only feedback is:
  - `QueueStatus` (FromRadio 11) on **every** send attempt, carrying
    `res` (0 = accepted) and `mesh_packet_id` = our id;
  - a `Routing` packet (portnum 5, `request_id` = our id) when the text is
    acked over the air, implicitly acked, or rejected with an error.
- Channel broadcast: encrypted with that channel's key by the node; the
  phone never touches the PSK.
- DM (to != broadcast, non-excluded portnum, node has a 32-byte private key):
  PKI-encrypted **iff the destination public key is in the node DB**
  (`wouldEncryptWithPKC`). On the PKI path a missing key is a hard
  `PKI_SEND_FAIL_PUBLIC_KEY` — it does **not** fall back to channel
  encryption. So only offer DMs to contacts whose `User.public_key` (field 8)
  we have seen (`Contact.hasKey`).
- Receiving: the node decrypts PKI/channel itself and hands the phone a
  `decoded` packet. A DM to us arrives as `packet{ decoded{ portnum=1 } }`
  with `to` = our num (fixed32) and `from` = sender (fixed32). Our parser
  only needs portnum 1 + payload + from/to.
- MeshPacket fields we must skip on RX: `public_key=16`, `pki_encrypted=17`,
  `rx_time=7` (fixed32), `rx_snr=8` (float/fixed32), `rx_rssi=12` (varint
  int32), etc. — all handled by the generic skipper.
- `NodeInfo`: `num=1` (varint), `user=2`, **`last_heard=5` (fixed32)** —
  not field 6 (`device_metrics`, a message). Parse field 5.

## 5. Node-side limits that bite

- **2-second text rate limit** (`PhoneAPI::handleToRadioPacket`): a second
  `TEXT_MESSAGE_APP` within 2 s is dropped with `RATE_LIMIT_EXCEEDED` (plus a
  QueueStatus + routing error). Pace client sends ≥2 s apart.
- **3-slot `fromPhoneQueue`**: BLE writes arriving faster than the node's
  main task drains them are dropped ("Drop ToRadio packet, fromPhoneQueue
  full"). One send at a time, paced, is the rule.
- **Duplicate ToRadio**: a write byte-identical to the previous one is
  dropped. The random `id` keeps every text unique.
- Broadcasts are never acked (want_ack cleared); DMs with want_ack produce a
  Routing ack on delivery or a routing error.

## 6. Heartbeat

`ToRadio.heartbeat` (field 7, `Heartbeat{ nonce=1 }`): nonce 0 is a
keepalive the node answers with a QueueStatus (proves the whole ToRadio
path); nonce 1 also re-broadcasts our NodeInfo. We heartbeat every 30 s in
READY. The reply comes through the `heartbeatReceived` path regardless of
PhoneAPI state, so it is the cheapest end-to-end health check.

## 7. Bugs found on our side (and fixed)

1. **`AppState::MESH_LINK_COMPOSE` had no renderer/handler.** Tapping
   MANUAL (or the old test bench at 14 s) entered it; the screen stayed blank
   and the board looked hung ("зависает на главном"). Fixed: a real case in
   the main switch renders the phone screen and, on OK, sends to the chat's
   target and returns to the chat.
2. **Missing `MeshPacket.from`.** Our writes were ACKed by BLE but produced
   no QueueStatus at all. Setting `from` = own node num fixed it.
3. **varint-encoded `from/to/id` (firmware ≥2.5 speaks fixed32).** Every
   incoming parsed as `from=0000/to=broadcast`: DMs showed as channel mail
   from "0000" and never reached the DM chat; Routing `request_id` never
   matched, so delivery was never confirmed; our own sends (varint
   `from/to`) were skipped by the node's decoder and dropped/misrouted.
   Fixed: encode fixed32, decode either wire; `Data.request_id` likewise;
   `NodeInfo.last_heard` is field 5 fixed32 (was read as field 6 varint —
   always 0).
4. **MTU left at 23.** Raised to 517 for the ~40-byte ToRadio writes.
5. **Link drops every ~minute: disconnect reason 520.** 520 = 0x208 =
   `BLE_HS_HCI_ERR(0x08)` = HCI Connection Timeout (supervision timer):
   the shared observer scan left running beside the GATT link starves its
   link-layer events on the single radio until the timer fires. Fix: the
   scan runs only while hunting -- off for the life of a link, back on for
   discovery/retries (`hwScanSet`). Detection never restarts a stopped scan
   itself; leaving COMPANION mode (`shutdown`) restores it for BROMESH.
   Also: an auth wait that times out (status 0, not just -1) is now a
   `pair timeout` failure instead of stumbling on unencrypted.
4. **Bench/test code force-entered compose and spam-sent.** Removed; sends
   now go only through the paced UI path.

## 8. UI conventions (companion panel)

The main-screen companion panel replaces the counter block in companion mode
(`drawCompanionPanel`, `ui_clear.cpp`). The app's button language:
`Theme::drawButton` = 1-px PURPLE border, BG fill, centred cyan label,
purple/white when pressed (used by the bottom `[SCAN][LOG][DESK]` bar and the
message screens); `Theme::drawWin95Button` = silver 2-px bevel + black label
(system dialogs). The panel must use `drawButton` to match, with pressed
feedback and the red unread rim on the messages key.
