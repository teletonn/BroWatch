// BroWatch — the MESH signatures.
//
// Value 5 used to be RAVEN (a US gunshot detector, unverified IDs, no RU
// presence). It is now MESH: Meshtastic, MeshCore companion radios and
// RNode/Reticulum, matched on 128-bit BLE service UUIDs (device-side in
// DetectionEngine, which host tests cannot link -- NimBLE) and on
// advertised names + WiFi SSIDs here, which this file pins.
//
// Ground rules, same as hacker_test.cpp: no invented constants. The
// Meshtastic service UUID comes from their firmware's BluetoothCommon.h,
// the NUS UUID from MeshCore's companion_protocol.md, the name formats
// from getDeviceName()/MyMesh.h/RNode_Firmware's Bluetooth.h, the
// MeshCore-OTA SSID from the EastMesh docs. Sources per
// docs/browatch/07-rossiya-detekt.md.
#include "signatures.h"
#include "test_util.h"
#include <cstring>

int main() {
    Confidence conf = Confidence::LOW_CONF;
    const uint8_t mac[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };

    suite("Meshtastic advertised names");

    // Default name: "Meshtastic_" + last 2 MAC bytes as hex.
    ck("default node name matches",
       lookupBtName("Meshtastic_ab13") == DetectionType::MESH);
    // Owner-renamed nodes keep the _xxxx suffix ("BOB_a1f2"); the 128-bit
    // service UUID still catches those on-device, but a bare name match
    // must not claim what it cannot prove -- only the stock prefix does.
    ck("case does not matter",
       lookupBtName("MESHTASTIC_AB13") == DetectionType::MESH);
    ck("a renamed node is not a name match (UUID catches it on-device)",
       lookupBtName("BOB_a1f2") == DetectionType::UNKNOWN);
    // KNOWN COST, accepted deliberately: the rule is a substring, so a
    // phone called "ILoveMeshtasticClub" matches at MED. Same trade as
    // Flipper's name rule -- the exact UUID is what carries the verdict.
    ck("substring rule fires anywhere (documented cost)",
       lookupBtName("ILoveMeshtasticClub") == DetectionType::MESH);

    suite("MeshCore / RNode advertised names");

    ck("MeshCore- prefix matches",
       lookupBtName("MeshCore-A1B2") == DetectionType::MESH);
    ck("third-party companion builds match too",
       lookupBtName("Whisper-01") == DetectionType::MESH);
    ck("WisCore matches",
       lookupBtName("WisCore-7f") == DetectionType::MESH);
    ck("LowMesh_MC_ matches",
       lookupBtName("LowMesh_MC_9") == DetectionType::MESH);
    ck("RNode with space matches",
       lookupBtName("RNode A1F2") == DetectionType::MESH);
    // Prefix-anchored: "RNode" inside another word is not a node.
    ck("RNode without the space does not",
       lookupBtName("RNodeA1F2") == DetectionType::UNKNOWN);
    ck("...nor inside another word",
       lookupBtName("Barnode X") == DetectionType::UNKNOWN);

    suite("Mesh SSIDs");

    // The only beacon SSID any of the three networks raises: an ESP32
    // MeshCore repeater doing OTA with no WiFi configured.
    ck("MeshCore-OTA is MESH", lookupSsid("MeshCore-OTA") == DetectionType::MESH);
    // Meshtastic and Reticulum raise no AP in normal operation, so there
    // is deliberately no SSID rule for them -- and legacy/custom SSIDs
    // somebody typed themselves must not match either.
    ck("no Meshtastic_* SSID rule", lookupSsid("Meshtastic_ab13") == DetectionType::UNKNOWN);
    ck("no RNode SSID rule", lookupSsid("RNode-net") == DetectionType::UNKNOWN);

    suite("The old Raven block stays free");

    for (uint16_t u = 0x3100; u <= 0x3500; u += 0x100)
        if (lookupUuid(u) != DetectionType::UNKNOWN) {
            char msg[64];
            snprintf(msg, sizeof(msg), "0x%04X matches nothing", u);
            ck(msg, false);
        }
    ck("0x3100..0x3500 all free", true);

    suite("MESH grades like a name match");

    // A name is a string anybody can set: MED, the exact radio signature
    // (Meshtastic's service UUID) grades HIGH at its own match site.
    ck("MESH base grade is MED", confidenceFor(DetectionType::MESH) == Confidence::MED_CONF);
    ck("lookupOui of nothing is still UNKNOWN",
       lookupOui(mac, &conf) == DetectionType::UNKNOWN);

    return report();
}
