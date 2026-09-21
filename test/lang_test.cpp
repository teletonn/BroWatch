// BroWatch — UI language setting (Settings::lang).
//
// What this guards: the RU/EN toggle is persisted NVS state like every
// other setting. Fresh boards speak EN (the stock default), cycling flips
// EN<->RU, setLang() refuses anything else, and the value survives a
// reload -- so a board set to Russian does not come back English.
#include "test_util.h"
#include "settings.h"
#include "theme.h"
#include "clock.h"
#include <Preferences.h>
#include <cstdlib>
#include <cstring>

// The two things settings.cpp reaches for that are not under test.
namespace Theme { void applyPalette(uint8_t) {} }
namespace Clock {
uint8_t     zoneCount()        { return 1; }
const char* zoneName(uint8_t)  { return "UTC"; }
void        applyZone(uint8_t) {}
}

int main() {
    setenv("SQUACHSIM_NVS", "out", 1);
    remove("out/settings.nvs");

    suite("Fresh board speaks English");
    Settings::load();
    ck("default is EN", Settings::lang() == 0);
    ck("label reads EN", strcmp(Settings::langName(), "EN") == 0);

    suite("Cycling flips EN<->RU");
    Settings::cycleLang();
    ck("after one tap RU", Settings::lang() == 1);
    ck("label reads RU", strcmp(Settings::langName(), "RU") == 0);
    Settings::cycleLang();
    ck("after two taps EN again", Settings::lang() == 0);

    suite("setLang refuses anything but 0/1");
    Settings::setLang(1);
    ck("set 1 sticks", Settings::lang() == 1);
    Settings::setLang(9);
    ck("set 9 ignored", Settings::lang() == 1);
    Settings::setLang(0);
    ck("set 0 sticks", Settings::lang() == 0);

    suite("The choice survives a restart");
    Settings::setLang(1);
    Settings::load();
    ck("still RU after reload", Settings::lang() == 1);
    Settings::setLang(0);
    Settings::load();
    ck("still EN after reload", Settings::lang() == 0);

    remove("out/settings.nvs");
    return report();
}
