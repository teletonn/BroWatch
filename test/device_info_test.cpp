// SquachWatch-CYD — every device has its own MORE INFO page, every page can
// be summoned in the emulator, and every page fits the panel.
//
// The failure this exists for already happened once: the web emulator's
// detection list was typed out by hand, stopped at EVILTWIN, and so nobody
// could look at a Flipper Zero's alert without a Flipper Zero. A table that
// has to agree with three others is only kept in step by a test.
#include "test_util.h"
#include "device_info.h"
#include "detection.h"
#include "detection_info.h"
#include "type_names.h"
#include "settings.h"
#include "ru_text.h"
#include "gfxff/gfxfont.h"
#define PROGMEM
#include "ru_font.h"
#include "signatures.h"
#include "sim_detections.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// settings.cpp reaches for these; same stubs as lang_test.cpp.
namespace Theme { void applyPalette(uint8_t) {} const char* tr(const char* en, const char*) { return en; } }
namespace Clock {
uint8_t     zoneCount()        { return 1; }
const char* zoneName(uint8_t)  { return "UTC"; }
void        applyZone(uint8_t) {}
}

using DeviceInfo::Device;

static bool hasPages(DetectionType t) {
    for (uint8_t i = 0; i < DeviceInfo::kDeviceCount; i++)
        if (DeviceInfo::kDevices[i].type == t) return true;
    return false;
}

// A signature row of a type with device pages must land on one.
static bool reaches(DetectionType t, const char* vendor, const char* name, const char* what) {
    if (!hasPages(t)) return true;
    if (DeviceInfo::find(t, vendor, name)) return true;
    printf("    no page for %s: %s vendor=\"%s\" name=\"%s\"\n",
           what, detectionTypeName(t), vendor, name);
    return false;
}

// What the BLE path writes as the vendor for a service-UUID or company-ID
// match -- the rule in detection.cpp's vendor block, restated.
static const char* bleVendor(DetectionType t, const char* rowLabel) {
    switch (t) {
        case DetectionType::META:    return rowLabel;
        case DetectionType::SKIMMER: return rowLabel;
        case DetectionType::FLOCK:   return "Flock-BLE";
        case DetectionType::HACKER:  return "Flipper";
        default:                     return rowLabel;
    }
}

// Greedy word wrap at `cols` characters: how many lines the panel needs.
static int wrapLines(const char* s, int cols) {
    int lines = 0, col = 0;
    while (*s) {
        while (*s == ' ') s++;
        const char* w = s;
        while (*s && *s != ' ') s++;
        const int n = (int)(s - w);
        if (!n) break;
        if (col == 0)                 { col = n; lines++; }
        else if (col + 1 + n <= cols) { col += 1 + n; }
        else                          { col = n; lines++; }
    }
    return lines;
}

// Bangers draws A-Z, 0-9, space, '!', and the hand-added hyphen and
// apostrophe. Anything else is skipped without a gap.
static bool bangersOk(const char* s) {
    for (; *s; s++)
        if (!(isupper((unsigned char)*s) || isdigit((unsigned char)*s) ||
              *s == ' ' || *s == '!' || *s == '-' || *s == '\'')) return false;
    return true;
}

// Greedy word wrap mirroring Theme::wrapTextRU() at size 1: words split on
// spaces, a line breaks past maxW pixels or 47 bytes, seven lines max with
// the tail dropped -- which the test reports instead of hiding.
static int ruPix(const char* s);
static void ruWrapLines(const char* text, int maxW, int& lines, int& dropped) {
    lines = 0; dropped = 0;
    char buf[320];
    strncpy(buf, text, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    char lineBuf[48] = "";
    char* word = strtok(buf, " ");
    while (word) {
        char trial[48];
        if (lineBuf[0]) snprintf(trial, sizeof(trial), "%s %s", lineBuf, word);
        else            snprintf(trial, sizeof(trial), "%s", word);
        bool tooWide = lineBuf[0] && (ruPix(trial) > maxW || strlen(trial) >= sizeof(lineBuf) - 1);
        if (tooWide) {
            if (lines >= 7 - 1) { dropped = 1; break; }
            lines++;
            strncpy(lineBuf, word, sizeof(lineBuf) - 1); lineBuf[sizeof(lineBuf) - 1] = 0;
        } else {
            strncpy(lineBuf, trial, sizeof(lineBuf) - 1); lineBuf[sizeof(lineBuf) - 1] = 0;
        }
        word = strtok(nullptr, " ");
    }
    if (lineBuf[0] && lines < 7) lines++;
}

// Ink width mirroring Theme::ruInk() at size 1.
static int ruPix(const char* s) {
    int pen = 0, right = 0;
    const uint8_t* p = (const uint8_t*)s;
    while (*p) {
        uint16_t cp;
        int adv = 6;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p & 0xE0) == 0xC0 && p[1]) {
            cp = ((p[0] & 0x1FU) << 6) | (p[1] & 0x3FU); p += 2;
        } else { cp = '?'; p++; }
        if (cp >= RuCyr8.first && cp <= RuCyr8.last) {
            const GFXglyph& g = RuCyr8Glyphs[cp - RuCyr8.first];
            const int ink = (int)g.xOffset + (int)g.width;
            adv = g.xAdvance > ink ? g.xAdvance : ink;
        }
        const int edge = pen + adv;
        if (edge > right) right = edge;
        pen += adv;
    }
    return right;
}

static bool hasCyrillic(const char* s) {
    const uint8_t* p = (const uint8_t*)s;
    while (*p) {
        if (*p < 0x80) { p++; continue; }
        if ((*p & 0xE0) == 0xC0 && p[1]) {
            unsigned cp = ((p[0] & 0x1FU) << 6) | (p[1] & 0x3FU);
            if (cp >= 0x400 && cp <= 0x45F) return true;
            p += 2;
        } else p++;
    }
    return false;
}

int main() {
    setenv("SQUACHSIM_NVS", "out", 1);
    remove("out/settings.nvs");
    Settings::load();

    suite("Every signature a multi-device type can log has its own page");
    {
        bool ok = true;
        for (uint16_t i = 0; i < kOuiCount; i++)
            ok &= reaches(kOuiTable[i].type, kOuiTable[i].name, "", "OUI row");
        for (uint16_t i = 0; i < kSsidCount; i++)
            ok &= reaches(kSsidPrefixes[i].type, kSsidPrefixes[i].name, "", "SSID row");
        for (uint16_t i = 0; i < kUuidCount; i++)
            ok &= reaches(kUuidTable[i].type, bleVendor(kUuidTable[i].type, kUuidTable[i].name),
                          "", "UUID row");
        for (uint16_t i = 0; i < kMfgIdCount; i++)
            ok &= reaches(kMfgIdTable[i].type, bleVendor(kMfgIdTable[i].type, kMfgIdTable[i].name),
                          "", "company-ID row");
        for (uint16_t i = 0; i < kBtClassicCount; i++)
            ok &= reaches(kBtClassicNames[i].type, bleVendor(kBtClassicNames[i].type, "BLE"),
                          kBtClassicNames[i].name, "Bluetooth name");
        ck("every row reaches a page", ok);
        ck("a pwnagotchi, labelled in processWiFiQ(), has its page",
           DeviceInfo::find(DetectionType::HACKER, "Pwnagotchi", "rikki") != nullptr);
        const Device* f = DeviceInfo::find(DetectionType::HACKER, "Flipper", "Flipper Ozzyx");
        ck("a Flipper Zero gets the Flipper Zero page", f && !strcmp(f->title, "FLIPPER ZERO"));
        ck("a type without device pages falls back to its own paragraph",
           DeviceInfo::find(DetectionType::AIRTAG, "Apple", "AirTag") == nullptr);
        const Device* b = DeviceInfo::find(DetectionType::FLOCK, "Flock-BLE", "FS Ext Battery");
        ck("a name wins over the generic label its type gets",
           b && !strcmp(b->title, "FLOCK POWER"));
        ck("a device name matches whatever its case",
           DeviceInfo::find(DetectionType::SKIMMER, "BLE", "hc-05") != nullptr);
    }

    suite("Every page can be summoned in the emulator");
    {
        bool ok = true;
        for (uint8_t i = 0; i < DeviceInfo::kDeviceCount; i++) {
            const Device& dv = DeviceInfo::kDevices[i];
            bool found = false;
            for (size_t p = 0; p < kSimProfileCount && !found; p++)
                found = DeviceInfo::find(kSimProfiles[p].type, kSimProfiles[p].vendor,
                                         kSimProfiles[p].name) == &dv;
            if (!found) { printf("    no emulator profile for %s\n", dv.title); ok = false; }
        }
        ck("every device page has a profile that reaches it", ok);
        bool types = true;
        for (uint8_t t = 1; t < (uint8_t)DetectionType::COUNT; t++)
            if (!simProfileFor((DetectionType)t)) {
                printf("    no emulator profile for %s\n", detectionTypeName((DetectionType)t));
                types = false;
            }
        ck("and every type has at least one", types);
        bool fits = true;
        // The name is copied into the Detection's own buffer, so it has to
        // fit. The vendor is not: it is a pointer to the profile's string.
        for (size_t p = 0; p < kSimProfileCount; p++)
            if (strlen(kSimProfiles[p].name) >= sizeof(Detection::name)) {
                printf("    profile %s overflows a Detection field\n", kSimProfiles[p].label);
                fits = false;
            }
        ck("every profile fits the Detection it builds", fits);
    }

    suite("Every page fits the MORE INFO panel");
    {
        bool text = true, title = true;
        for (uint8_t i = 0; i < DeviceInfo::kDeviceCount; i++) {
            const Device& dv = DeviceInfo::kDevices[i];
            // Portrait: a 208 px text column is 34 characters, seven lines.
            const int n = wrapLines(dv.text, 34);
            if (n > 7) { printf("    %s runs to %d lines in portrait\n", dv.title, n); text = false; }
            if (strlen(dv.title) > 13 || !bangersOk(dv.title)) {
                printf("    title \"%s\" is too long or has a character Bangers skips\n", dv.title);
                title = false;
            }
        }
        ck("seven lines of 34 or fewer", text);
        ck("titles of 13 characters Bangers can draw", title);
    }

    suite("Every RU page fits the MORE INFO panel");
    {
        // Pixel-exact mirror of Theme::ruInk() at size 1: ASCII costs the
        // 6px GLCD cell, Cyrillic its ink extent from RuCyr8, anything else
        // the 6px '?' fallback. Portrait text column is 208 px, seven lines.
        bool fit = true, charset = true, bytes = true, nonempty = true;
        for (uint8_t i = 0; i < DeviceInfo::kDeviceCount; i++) {
            const Device& dv = DeviceInfo::kDevices[i];
            const char* s = dv.textRU;
            if (!s || !s[0]) { printf("    %s has no RU text\n", dv.title); nonempty = false; continue; }
            if (strlen(s) >= 320) { printf("    %s RU overflows the 320-byte wrap buffer\n", dv.title); bytes = false; }
            const uint8_t* p = (const uint8_t*)s;
            while (*p) {
                uint16_t cp;
                if (*p < 0x80) { cp = *p++; }
                else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 0x1FU) << 6) | (p[1] & 0x3FU); p += 2; }
                else { cp = 0xFFFF; p++; }
                if (cp >= 0x80 && !(cp >= RuCyr8.first && cp <= RuCyr8.last)) {
                    printf("    %s RU has a glyph the RU face cannot draw (U+%04X)\n", dv.title, cp);
                    charset = false;
                    break;
                }
            }
            int lines = 0, dropped = 0;
            ruWrapLines(s, 208, lines, dropped);
            if (lines > 7 || dropped) {
                printf("    %s RU runs to %d lines%s\n", dv.title, lines, dropped ? " (tail dropped)" : "");
                fit = false;
            }
        }
        ck("RU text present everywhere", nonempty);
        ck("RU text under 320 bytes", bytes);
        ck("RU text is ASCII + RuCyr8 only", charset);
        ck("RU pages wrap to seven 208px lines", fit);
    }

    suite("RU type paragraphs route and fit");
    {
        Settings::setLang(1);
        bool fit = true, present = true;
        for (uint8_t t = 0; t < (uint8_t)DetectionType::COUNT; t++) {
            const char* s = DetectionInfo::explain((DetectionType)t);
            if (!s || !s[0] || !hasCyrillic(s)) {
                printf("    %s has no RU paragraph\n", detectionTypeName((DetectionType)t));
                present = false;
                continue;
            }
            if (strlen(s) >= 320) { printf("    %s RU overflows 320 bytes\n", detectionTypeName((DetectionType)t)); fit = false; }
            int lines = 0, dropped = 0;
            ruWrapLines(s, 208, lines, dropped);
            if (lines > 7 || dropped) {
                printf("    %s RU runs to %d lines%s\n", detectionTypeName((DetectionType)t), lines, dropped ? " (tail dropped)" : "");
                fit = false;
            }
        }
        const char* primer = DetectionInfo::rssiConfidencePrimer();
        bool primerOk = primer && primer[0] && hasCyrillic(primer) && strlen(primer) < 320;
        int pl = 0, pd = 0;
        if (primerOk) { ruWrapLines(primer, 208, pl, pd); primerOk = pl <= 7 && !pd; }
        ck("every type has a RU paragraph", present);
        ck("RU paragraphs fit the panel", fit);
        ck("RU primer fits too", primerOk);
        Settings::setLang(0);
        bool en = true;
        for (uint8_t t = 0; t < (uint8_t)DetectionType::COUNT; t++)
            if (hasCyrillic(DetectionInfo::explain((DetectionType)t))) { en = false; break; }
        if (hasCyrillic(DetectionInfo::rssiConfidencePrimer())) en = false;
        ck("EN mode shows no Cyrillic", en);
        const Device* f = DeviceInfo::find(DetectionType::HACKER, "Flipper", "Flipper Ozzyx");
        Settings::setLang(1);
        DetectionEngine eng{};
        ck("RU device page answers in Russian",
           f && DetectionInfo::explainFor(DetectionType::HACKER, "Flipper", "Flipper Ozzyx", eng) == f->textRU);
        Settings::setLang(1);
    }

    return report();
}
