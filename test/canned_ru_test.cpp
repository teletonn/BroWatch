// BroWatch — Russian canned lines mirror the English ones.
//
// What this guards: the air carries an INDEX, so CANNED_RU must have the
// same order and count as CANNED -- a RU board shows CANNED_RU[N] for the
// same N an EN board shows CANNED[N] as. Every RU line must also fit the
// UI that shows it: under 48 bytes (the wrap buffers) and short enough in
// glyphs for one picker row and the send confirmation.
#include "test_util.h"
#include "meshmsg.h"
#include "ru_text.h"
#include <cstring>

int main() {
    suite("Same indices, same count");
    ck("48 EN lines", MeshMsg::CANNED_N == 48);
    ck("48 RU lines", MeshMsg::CANNED_RU_N == 48);
    ck("counts match", MeshMsg::CANNED_RU_N == MeshMsg::CANNED_N);

    suite("Display routing");
    bool routed = true;
    for (uint8_t i = 0; i < MeshMsg::CANNED_N; i++) {
        if (MeshMsg::cannedDisplay(i, 0) != MeshMsg::CANNED[i]) routed = false;
        if (MeshMsg::cannedDisplay(i, 1) != MeshMsg::CANNED_RU[i]) routed = false;
    }
    ck("EN shows CANNED, RU shows CANNED_RU", routed);
    ck("lang 9 falls back to EN", MeshMsg::cannedDisplay(3, 9) == MeshMsg::CANNED[3]);
    ck("bad index falls back to 0", MeshMsg::cannedDisplay(200, 0) == MeshMsg::CANNED[0]);
    ck("bad index RU falls back to 0", MeshMsg::cannedDisplay(200, 1) == MeshMsg::CANNED_RU[0]);

    suite("Tabs route too");
    bool tabs = true;
    for (uint8_t i = 0; i < MeshMsg::CANNED_TABS; i++) {
        if (MeshMsg::cannedTabName(i, 0) != MeshMsg::CANNED_TAB_NAME[i]) tabs = false;
        if (MeshMsg::cannedTabName(i, 1) != MeshMsg::CANNED_TAB_NAME_RU[i]) tabs = false;
        if (!MeshMsg::CANNED_TAB_NAME_RU[i][0]) tabs = false;
    }
    ck("6 tabs both languages, none empty", tabs);
    ck("bad tab reads ?", strcmp(MeshMsg::cannedTabName(9, 1), "?") == 0);

    suite("Tab slots still point at real lines");
    bool slots = true;
    for (uint8_t tb = 0; tb < MeshMsg::CANNED_TABS; tb++)
        for (uint8_t s = 0; s < MeshMsg::CANNED_PER_TAB; s++) {
            const uint8_t idx = MeshMsg::cannedAtTab(tb, s);
            if (idx != 0xFF && idx >= MeshMsg::CANNED_N) slots = false;
        }
    ck("every slot is 0xFF or a valid index", slots);

    suite("RU lines fit the UI");
    bool fit = true, glyphs = true, cyr = true;
    for (uint8_t i = 0; i < MeshMsg::CANNED_RU_N; i++) {
        const char* s = MeshMsg::CANNED_RU[i];
        if (!s[0] || strlen(s) >= 48) fit = false;
        if (RuText::count(s) > 24) glyphs = false;
        // Every letter must be renderable: ASCII or the RuCyr8 block.
        const char* p = s;
        while (*p) {
            const uint16_t cp = RuText::next(&p);
            if (!RuText::isAscii(cp) && !RuText::isCyrillic(cp) &&
                cp != (uint16_t)'?' && cp != (uint16_t)'!') cyr = false;
        }
    }
    ck("all under 48 bytes", fit);
    ck("all within 24 glyphs", glyphs);
    ck("all renderable (ASCII/Cyrillic)", cyr);

    return report();
}
