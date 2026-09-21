// BroWatch — RuText UTF-8 decoder checks (header-only, no firmware sources).
#include "test_util.h"
#include "ru_text.h"
#include <cstring>

int main() {
    suite("ASCII passes through");
    {
        const char* p = "AZ 09";
        ck("A decodes", RuText::next(&p) == 'A');
        ck("Z decodes", RuText::next(&p) == 'Z');
        ck("space decodes", RuText::next(&p) == ' ');
        ck("count is glyphs", RuText::count("AZ 09") == 5);
    }

    suite("Cyrillic decodes to U+04xx");
    {
        const char* p = "АяЁё";  // U+0410 U+044F U+0401 U+0451
        ck("A-cyrillic U+0410", RuText::next(&p) == 0x0410);
        ck("ya-cyrillic U+044F", RuText::next(&p) == 0x044F);
        ck("Yo U+0401", RuText::next(&p) == 0x0401);
        ck("yo U+0451", RuText::next(&p) == 0x0451);
        ck("4 letters, 8 bytes", RuText::count("АяЁё") == 4);
        ck("strlen would say 8", strlen("АяЁё") == 8);
    }

    suite("Routing predicates");
    ck("U+0410 is Cyrillic", RuText::isCyrillic(0x0410));
    ck("U+0401 is Cyrillic", RuText::isCyrillic(0x0401));
    ck("A is not Cyrillic", !RuText::isCyrillic('A'));
    ck("A is ASCII", RuText::isAscii('A'));
    ck("U+0410 is not ASCII", !RuText::isAscii(0x0410));

    suite("Broken bytes never hang, never pass through");
    {
        const char* p = "\xFF\xFE";
        ck("0xFF -> ?", RuText::next(&p) == '?');
        ck("0xFE -> ?", RuText::next(&p) == '?');
        ck("consumed both", *p == '\0');
        const char* q = "A\xD0";  // truncated 2-byte sequence
        ck("A ok", RuText::next(&q) == 'A');
        ck("truncated -> ?", RuText::next(&q) == '?');
        ck("count counts the ? too", RuText::count("A\xD0") == 2);
    }

    suite("Mixed string measures glyphs");
    ck("\"СКАН 12\" is 7 glyphs", RuText::count("СКАН 12") == 7);

    return report();
}
