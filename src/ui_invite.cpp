// SquachWatch-CYD — the ADD TO SQUAD screen. See ui_invite.h.
#if SQUACH_MESH
#include "ui_invite.h"
#include "theme.h"
#include "settings.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {
using MeshTalk::InviteState;

const int BTN_H = 28;
const int SLOP  = 6;

bool        s_demo = false;
InviteState s_demoState = InviteState::IDLE;
uint16_t    s_demoCode = 0;
char        s_demoName[13] = "";
bool        s_demoInviter = false;
bool        s_showPhrase = false;

InviteState st()      { return s_demo ? s_demoState : MeshTalk::inviteState(); }
uint16_t    code()    { return s_demo ? s_demoCode : MeshTalk::inviteCode(); }
const char* peer()    { return s_demo ? s_demoName : MeshTalk::invitePeerName(); }
bool        inviter() { return s_demo ? s_demoInviter : MeshTalk::inviteIsInviter(); }

struct Btns { int y, w, leftX, rightX, oneX; };

Btns btns(TFT_eSPI& t) {
    Btns b;
    b.w = 110;
    const int gap = 14;
    b.leftX  = (t.width() - (b.w * 2 + gap)) / 2;
    b.rightX = b.leftX + b.w + gap;
    b.oneX   = (t.width() - b.w) / 2;
    b.y      = t.height() - BTN_H - 8;
    return b;
}

bool in(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx - SLOP && x <= bx + bw + SLOP && y >= by - SLOP && y <= by + bh + SLOP;
}

void centred(TFT_eSPI& t, int y, uint16_t c, const char* s) {
    t.setTextColor(c, Theme::BG);
    t.setCursor((t.width() - Theme::textWidthRU(t, s)) / 2, y);
    Theme::printRU(t, s);
}

// Which buttons the current page has: two, one, or none, and their labels.
struct Page { const char* left; const char* right; const char* one; };

Page page() {
    const bool inv = inviter();
    const bool ru = Settings::lang() == 1;
    const char* CANCEL = ru ? "ОТМЕНА" : "CANCEL";
    const char* BACK = ru ? "НАЗАД" : "BACK";
    switch (st()) {
        case InviteState::OFFERING: return { nullptr, nullptr, CANCEL };
        case InviteState::ASKED:    return { ru ? "ПРИНЯТЬ" : "ACCEPT", ru ? "ОТКАЗ" : "DECLINE", nullptr };
        case InviteState::CODE:     return { ru ? "СХОДИТСЯ" : "MATCHES", ru ? "НЕТ" : "NO", nullptr };
        case InviteState::SENDING:  return { nullptr, nullptr, ru ? "ГОТОВО" : "DONE" };
        case InviteState::WAITING:  return { nullptr, nullptr, CANCEL };
        case InviteState::JOINED:   return { nullptr, nullptr, ru ? "ОК" : "OK" };
        case InviteState::DONE:     return { nullptr, nullptr, BACK };
        case InviteState::FAILED:
            if (s_showPhrase)          return { nullptr, nullptr, BACK };
            return (inv && Settings::phraseShown()) ? Page{ ru ? "ПОКАЗАТЬ ФРАЗУ" : "SHOW PHRASE", BACK, nullptr } : Page{ nullptr, nullptr, BACK };
        default:                    return { nullptr, nullptr, BACK };
    }
}
} // namespace

void uiInviteDemo(InviteState s, uint16_t c, const char* name, bool inv) {
    s_demo = true; s_demoState = s; s_demoCode = c; s_demoInviter = inv;
    snprintf(s_demoName, sizeof s_demoName, "%s", name ? name : "");
}

void uiInviteInit(TFT_eSPI& t) {
    s_demo = false;
    s_showPhrase = false;
    t.fillRect(0, 0, t.width(), t.height(), Theme::BG);
}

bool uiInviteShowingPhrase() { return s_showPhrase; }

void uiInviteTick(TFT_eSPI& t, uint32_t now, const DetectionEngine& eng) {
    (void)eng;
    const int w = t.width(), h = t.height();
    t.fillRect(0, 0, w, h, Theme::BG);
    Theme::drawListHeading(t, Theme::tr("ADD TO SQUAD", "В ОТРЯД"), Theme::VAPOR_PINK);
    t.setTextSize(1);
    int y = Theme::LIST_TOP + Theme::LIST_HEADING_H + 10;
    char line[64];
    const char* who = peer()[0] ? peer() : Theme::tr("SOMEONE", "КТО-ТО");
    const Btns b = btns(t);
    const int midY = y + 36 + (b.y - (y + 36) - 16) / 2;   // where a big line sits
    const bool ru = Settings::lang() == 1;

    switch (st()) {
        case InviteState::OFFERING:
            if (ru) snprintf(line, sizeof line, "Спрашиваю плату %s.", who);
            else    snprintf(line, sizeof line, "Asking %s's board.", who);
            centred(t, y, Theme::WHITE, line);
            centred(t, y + 12, Theme::W95_LIGHT, Theme::tr("It will ask them to accept.", "Она спросит их согласия."));
            centred(t, y + 24, Theme::W95_LIGHT, Theme::tr("Keep the boards close.", "Держи платы рядом."));
            {
                // A slow dot march, so a still screen still reads as waiting.
                const int n = (int)((now / 400) % 4);
                char dots[8] = "";
                for (int i = 0; i < n; i++) dots[i] = '.';
                t.setTextSize(2);
                centred(t, midY, Theme::CYAN, dots[0] ? dots : " ");
                t.setTextSize(1);
            }
            break;
        case InviteState::ASKED:
            if (ru) snprintf(line, sizeof line, "%s зовёт тебя", who);
            else    snprintf(line, sizeof line, "%s wants to add you", who);
            centred(t, y, Theme::WHITE, line);
            centred(t, y + 12, Theme::WHITE, Theme::tr("to their squad.", "к себе в отряд."));
            centred(t, y + 30, Theme::W95_LIGHT, Theme::tr("You'd get their phrase: their", "Ты получишь их фразу:"));
            centred(t, y + 42, Theme::W95_LIGHT, Theme::tr("messages, visits and updates.", "сообщения, визиты, обновы."));
            centred(t, y + 54, Theme::W95_LIGHT, Theme::tr("Only if you know who this is.", "Только если знаешь, кто это."));
            break;
        case InviteState::CODE: {
            centred(t, y, Theme::WHITE, Theme::tr("Both boards show four digits.", "На платах по 4 цифры."));
            centred(t, y + 12, Theme::WHITE, Theme::tr("Read yours out. Do they match?", "Прочти вслух. Сходятся?"));
            t.setTextSize(3);
            snprintf(line, sizeof line, "%04u", (unsigned)code());
            centred(t, midY - 6, Theme::CYAN, line);
            t.setTextSize(1);
            centred(t, b.y - 16, Theme::W95_LIGHT, Theme::tr("Different digits means somebody is in the middle.", "Разные цифры — кто-то влез посередине."));
            break;
        }
        case InviteState::SENDING:
            if (ru) snprintf(line, sizeof line, "Шлю фразу: %s.", who);
            else    snprintf(line, sizeof line, "Sending the phrase to %s.", who);
            centred(t, y, Theme::WHITE, line);
            centred(t, y + 12, Theme::W95_LIGHT, Theme::tr("Their board says when it has it.", "Их плата скажет, как получит."));
            centred(t, y + 24, Theme::W95_LIGHT, Theme::tr("Half a minute at most.", "Не дольше полминуты."));
            break;
        case InviteState::WAITING:
            centred(t, y, Theme::WHITE, Theme::tr("Waiting for the phrase.", "Жду фразу."));
            if (ru) snprintf(line, sizeof line, "Плата %s шлёт её.", who);
            else    snprintf(line, sizeof line, "%s's board is sending it.", who);
            centred(t, y + 12, Theme::W95_LIGHT, line);
            break;
        case InviteState::JOINED:
            if (ru) snprintf(line, sizeof line, "Ты в отряде %s.", who);
            else    snprintf(line, sizeof line, "You're in %s's squad.", who);
            t.setTextSize(2);
            centred(t, midY - 8, Theme::GREEN, Theme::tr("YOU'RE IN", "ТЫ ВНУТРИ"));
            t.setTextSize(1);
            centred(t, y, Theme::WHITE, line);
            centred(t, y + 12, Theme::W95_LIGHT, Theme::tr("Messages and visits are on.", "Сообщения и визиты вкл."));
            break;
        case InviteState::DONE:
            if (MeshTalk::inviteConfirmed()) {
                if (ru) snprintf(line, sizeof line, "%s в твоём отряде.", who);
                else    snprintf(line, sizeof line, "%s is in your squad.", who);
                t.setTextSize(2);
                centred(t, midY - 8, Theme::GREEN, Theme::tr("ADDED", "ПРИНЯТ"));
                t.setTextSize(1);
                centred(t, y, Theme::WHITE, line);
                centred(t, y + 12, Theme::W95_LIGHT, Theme::tr("Messages and visits are on.", "Сообщения и визиты вкл."));
            } else {
                if (ru) snprintf(line, sizeof line, "Фраза ушла: %s,", who);
                else    snprintf(line, sizeof line, "The phrase went out to %s,", who);
                centred(t, y, Theme::WHITE, line);
                centred(t, y + 12, Theme::WHITE, Theme::tr("but their board has not answered.", "но их плата молчит."));
                centred(t, y + 24, Theme::W95_LIGHT, Theme::tr("Check their screen. If it did not", "Глянь на их экран. Если не"));
                centred(t, y + 36, Theme::W95_LIGHT, Theme::tr("take, SHOW PHRASE and read it out.", "вышло — покажи фразу вслух."));
            }
            break;
        case InviteState::FAILED:
            if (s_showPhrase) {
                centred(t, y, Theme::WHITE, Theme::tr("Read this out. They type it", "Прочти вслух. Они вобьют"));
                centred(t, y + 12, Theme::WHITE, Theme::tr("under BROMESH, PHRASE.", "её в BROMESH, PHRASE."));
                // Five words on two lines, big enough to read across a table.
                {
                    const char* p = MeshTalk::phrase();
                    char l1[24] = "", l2[24] = "";
                    const size_t n = strlen(p);
                    size_t cut = n;
                    if (n > 20) { cut = 20; while (cut > 0 && p[cut] != ' ') cut--; if (cut == 0) cut = 20; }
                    snprintf(l1, sizeof l1, "%.*s", (int)cut, p);
                    snprintf(l2, sizeof l2, "%s", p + cut + (p[cut] == ' ' ? 1 : 0));
                    t.setTextSize(2);
                    centred(t, midY - 12, Theme::CYAN, l1);
                    if (l2[0]) centred(t, midY + 8, Theme::CYAN, l2);
                    t.setTextSize(1);
                }
            } else {
                centred(t, y, Theme::AMBER, Theme::tr("That didn't work.", "Не вышло."));
                centred(t, y + 12, Theme::WHITE, MeshTalk::inviteWhy());
                if (inviter() && Settings::phraseShown()) {
                    centred(t, y + 30, Theme::W95_LIGHT, Theme::tr("The sure way: show them the phrase", "Надёжно: покажи им фразу,"));
                    centred(t, y + 42, Theme::W95_LIGHT, Theme::tr("and let them type it.", "пусть вобьют сами."));
                } else if (inviter()) {
                    centred(t, y + 30, Theme::W95_LIGHT, Theme::tr("This board keeps its phrase hidden.", "Эта плата фразу прячет."));
                    centred(t, y + 42, Theme::W95_LIGHT, Theme::tr("Move closer and try again.", "Подойди ближе и повтори."));
                }
            }
            break;
        default:
            centred(t, y, Theme::W95_LIGHT, "Nothing going on.");
            break;
    }

    const Page pg = page();
    if (pg.one)  Theme::drawWin95Button(t, b.oneX, b.y, b.w, BTN_H, pg.one, false);
    if (pg.left) Theme::drawWin95Button(t, b.leftX, b.y, b.w, BTN_H, pg.left, false);
    if (pg.right) Theme::drawWin95Button(t, b.rightX, b.y, b.w, BTN_H, pg.right, false);
}

InviteHit uiInviteHit(TFT_eSPI& t, int x, int y) {
    const Btns b = btns(t);
    const Page pg = page();
    const bool one = pg.one && in(x, y, b.oneX, b.y, b.w, BTN_H);
    const bool l   = pg.left && in(x, y, b.leftX, b.y, b.w, BTN_H);
    const bool r   = pg.right && in(x, y, b.rightX, b.y, b.w, BTN_H);
    switch (st()) {
        case InviteState::OFFERING: return one ? InviteHit::CANCEL : InviteHit::NONE;
        case InviteState::ASKED:    return l ? InviteHit::ACCEPT : (r ? InviteHit::DECLINE : InviteHit::NONE);
        case InviteState::CODE:     return l ? InviteHit::MATCH  : (r ? InviteHit::NOMATCH : InviteHit::NONE);
        case InviteState::SENDING:  return one ? InviteHit::BACK : InviteHit::NONE;
        case InviteState::WAITING:  return one ? InviteHit::CANCEL : InviteHit::NONE;
        case InviteState::JOINED:
        case InviteState::DONE:     return one ? InviteHit::BACK : InviteHit::NONE;
        case InviteState::FAILED:
            if (s_showPhrase || !inviter() || !Settings::phraseShown()) return one ? InviteHit::BACK : InviteHit::NONE;
            if (l) { s_showPhrase = true; return InviteHit::SHOW; }
            return r ? InviteHit::BACK : InviteHit::NONE;
        default:                    return one ? InviteHit::BACK : InviteHit::NONE;
    }
}
#endif // SQUACH_MESH
