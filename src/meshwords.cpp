// SquachWatch-CYD — the SquachMesh phrase words and the canned lines.
//
// THE WORDS. A phrase is five of these, rolled by the device's hardware random
// number generator -- never chosen by a person, which is where nearly all of a
// phrase's strength would otherwise go. 314 words is about 41.5 bits per
// phrase. The rules, each checked by test/meshmsg_test.cpp except the last:
//
//   - capital letters only, three to eight of them, so every word fits a
//     picker cell and reads the same on every screen
//   - no two words share their first three letters, so a word can always be
//     named by its start, and a half-heard word is never two candidates
//   - no letter has more than 18 words, so each letter's words fit on one
//     screen of the picker at either rotation
//   - kept in alphabetical order, which is the order the picker shows them in
//   - spelled the way it is said, with no sound-alikes: read aloud across a
//     table, FREQ and PHREAK are the same word, so FREQ is not here; XENON
//     starts with a Z sound and the listener looks under the wrong letter,
//     so it is not here either
//
// Keys are derived from a phrase's TEXT, not from word positions, so words can
// be ADDED anywhere later without breaking a single existing phrase. Renaming
// or removing one breaks every phrase that used it. Don't.
//
// THE CANNED LINES are the other way round: a message carries an INDEX, so
// both ends must agree what index N means. Append only.
#include "meshmsg.h"

#if SQUACH_MESH

namespace MeshMsg {

const char* const WORDS[] = {
    "ABDUCT",   "ACIDBURN", "ALIEN",    "AMBER",    "ANALOG",   "ANTENNA",
    "APOLLO",   "ARCADE",   "ARGON",    "ASTRO",    "ATLAS",    "AURORA",
    "AVALON",   "AXIOM",
    "BACKDOOR", "BASILISK", "BAUD",     "BEACON",   "BIGFOOT",  "BINARY",
    "BLACKICE", "BLUEBOX",  "BOOTLEG",  "BRAMBLE",  "BUFFER",   "BUNYIP",
    "BYPASS",
    "CABLE",    "CARRIER",  "CASSETTE", "CEREAL",   "CHANNEL",  "CHIPTUNE",
    "CIPHER",   "CIRCUIT",  "COBALT",   "CODEC",    "COMET",    "CONSOLE",
    "CORTEX",   "COYOTE",   "CRASH",    "CRYPTID",  "CURSOR",   "CYBER",
    "DAEMON",   "DAVINCI",  "DECODER",  "DEEPWOOD", "DELTA",    "DIALTONE",
    "DIGITAL",  "DISKETTE", "DOGMAN",   "DOPPLER",  "DOVER",    "DRONE",
    "DUSK",     "DYNAMO",
    "ECHO",     "ECLIPSE",  "EMBER",    "ENCODE",   "ENIGMA",   "EPOCH",
    "ESCAPE",   "ETHER",    "EXODUS",   "EXPLOIT",  "EYESHINE",
    "FALCON",   "FIBER",    "FIREWALL", "FLATWOOD", "FLOCK",    "FLUX",
    "FOGBANK",  "FORTRAN",  "FOXFIRE",  "FROST",    "FUSION",   "FUZZ",
    "GADGET",   "GAMMA",    "GARBAGE",  "GHOST",    "GIBSON",   "GLITCH",
    "GOBLIN",   "GOPHER",   "GRAVITY",  "GRID",     "GRUNGE",   "GYRO",
    "HACKER",   "HALO",     "HANDLE",   "HARBOR",   "HEX",      "HIDEOUT",
    "HODAG",    "HOLLOW",   "HORIZON",  "HOTWIRE",  "HOWLER",   "HUNTER",
    "HYPER",
    "ICEBOX",   "IMPULSE",  "INFRARED", "INSOMNIA", "ION",      "IRIDIUM",
    "IRONWOOD", "ISOTOPE",  "IVORY",
    "JACKPOT",  "JAMMER",   "JERSEY",   "JETSET",   "JIGSAW",   "JOEY",
    "JOYSTICK", "JUKEBOX",  "JUNGLE",   "JUPITER",
    "KERNEL",   "KEYGEN",   "KILOBYTE", "KINETIC",  "KITE",     "KLAXON",
    "KRAKEN",   "KRYPTON",  "KUDZU",
    "LANTERN",  "LASER",    "LATENCY",  "LEGEND",   "LICHEN",   "LIMINAL",
    "LITHIUM",  "LOCKPICK", "LOGIC",    "LUNAR",    "LURKER",   "LYNX",
    "MAGNET",   "MALWARE",  "MANTIS",   "MATRIX",   "MEGABYTE", "MERIDIAN",
    "MESH",     "MICRO",    "MIDNIGHT", "MIRAGE",   "MODEM",    "MOKELE",
    "MONOLITH", "MOTHMAN",  "MUTANT",   "MYSTERY",
    "NANO",     "NEBULA",   "NEON",     "NESSIE",   "NETWORK",  "NEXUS",
    "NIKON",    "NIMBUS",   "NOISE",    "NOMAD",    "NORTH",    "NOVA",
    "NULL",
    "OBSIDIAN", "OCTANE",   "ODYSSEY",  "OGOPOGO",  "OMEGA",    "ONYX",
    "OPCODE",   "ORACLE",   "ORBIT",    "OSMIUM",   "OUTPOST",  "OVERRIDE",
    "OWLMAN",   "OXYGEN",   "OZONE",
    "PACKET",   "PAGER",    "PARADOX",  "PATCH",    "PAYLOAD",  "PHANTOM",
    "PHREAK",   "PINBALL",  "PIXEL",    "PLAGUE",   "PLUTO",    "POLARIS",
    "PORTAL",   "PROXY",    "PULSAR",   "PYLON",
    "QUANTUM",  "QUEST",    "QUIVER",   "QWERTY",
    "RADAR",    "RAINFALL", "RANGER",   "RAVEN",    "RECON",    "REDWOOD",
    "RELAY",    "REMOTE",   "RETRO",    "RIDDLE",   "ROBOT",    "ROCKET",
    "ROGUE",    "ROOTKIT",  "ROUTER",   "RUNWAY",   "RUSTLE",
    "SALVAGE",  "SATURN",   "SCANNER",  "SECTOR",   "SENTINEL", "SERVER",
    "SHADOW",   "SIGNAL",   "SILICON",  "SKUNKAPE", "SLEUTH",   "SMOKE",
    "SONAR",    "SPECTRUM", "SQUACHY",  "STATIC",   "SUBNET",   "SYNTH",
    "TACHYON",  "TALON",    "TANGO",    "TELNET",   "TERMINAL", "TESLA",
    "THUNDER",  "TIMBER",   "TOKEN",    "TORRENT",  "TOTEM",    "TRACKER",
    "TRIGGER",  "TROJAN",   "TUNDRA",   "TURBO",    "TWILIGHT",
    "UFO",      "ULTRA",    "UMBRA",    "UNDERTOW", "UNICODE",  "UPLINK",
    "URANIUM",  "UTOPIA",
    "VACUUM",   "VALVE",    "VAPOR",    "VECTOR",   "VELVET",   "VENOM",
    "VERTEX",   "VHS",      "VIPER",    "VIRUS",    "VISOR",    "VOLTAGE",
    "VORTEX",   "VOXEL",    "VULCAN",
    "WALKMAN",  "WARDRIVE", "WAVEFORM", "WENDIGO",  "WEREWOLF", "WHISPER",
    "WIDGET",   "WILDFIRE", "WINDMILL", "WIRETAP",  "WIZARD",   "WOLFPACK",
    "WORMHOLE",
    "XEROX",    "XRAY",
    "YETI",     "YONDER",   "YOWIE",    "YUKON",
    "ZENITH",   "ZEPHYR",   "ZEROCOOL", "ZIGZAG",   "ZIPPER",   "ZODIAC",
    "ZOMBIE",   "ZONE",     "ZULU",
};
const uint16_t WORD_N = (uint16_t)(sizeof(WORDS) / sizeof(WORDS[0]));

// Short enough for one line of one speech bubble at either rotation. Practical
// first, because a message is for saying something; a few in character,
// because it is still Squachy saying it.
const char* const CANNED[] = {
    "On my way.",
    "Where are you?",
    "All clear here.",
    "Something's nearby.",
    "Heading out.",
    "Be right back.",
    "Yes.",
    "No.",
    "Maybe.",
    "Meet at the car.",
    "Watch your back.",
    "Camera on my left.",
    "Found a Flock cam.",
    "Stay put.",
    "Come to me.",
    "Leaving now.",
    "Five minutes.",
    "Running late.",
    "Ha.",
    "Nice.",
    "Thanks.",
    "Snacks?",
    "Is it following you?",
    "Going dark.",
    // ---- 24..47, added 2026-09-12 --------------------------------------------
    // The first twenty-four were written to prove messaging worked. These are
    // written to be USED: what you actually need to say to somebody else
    // carrying one of these, in the situations this device exists for.
    // Appended, never inserted -- see the index note at the top of this file.
    "Cop car ahead.",
    "Drone overhead.",
    "ALPR on the pole.",
    "Two of them now.",
    "It's gone now.",
    "I'm safe.",
    "Don't come here.",
    "Turn around.",
    "Being followed.",
    "You okay?",
    "Still there?",
    "Can you talk?",
    "Which way?",
    "How many?",
    "Need a ride?",
    "Call when you can.",
    "Got it.",
    "On it.",
    "Not yet.",
    "Copy that.",
    "Squatch out.",
    "Big if true.",
    "Beep boop.",
    "Stay squachy.",
    // ---- 48..49, reactions (BroWatch) -----------------------------------------
    // Answers, not openers: sent from an incoming message's own buttons, so
    // they live in NO picker's tab on purpose (see cannedAtTab below and
    // the tab test). Appended like everything else -- an older build reads
    // them as UNKNOWN_LINE: recorded, shown as unknown, never crashed on.
    // Twenty characters max like every other line: the other board's bubble.
    "Liked it.",
    "Didn't like it.",
};
const uint8_t CANNED_N = (uint8_t)(sizeof(CANNED) / sizeof(CANNED[0]));

// ---- Russian display strings (BroWatch) --------------------------------------
// Same indices, same order as CANNED above -- the air carries the index, so
// both languages always mean the same thing. Adapted, not translated word
// for word: the same situation, the way it is said in a Russian chat.
// Short on purpose: one speech-bubble line, one picker row.
const char* const CANNED_RU[] = {
    "Уже еду.",            // 0  On my way.
    "Ты где?",             // 1  Where are you?
    "У меня чисто.",       // 2  All clear here.
    "Тут что-то есть.",    // 3  Something's nearby.
    "Выхожу.",             // 4  Heading out.
    "Ща вернусь.",         // 5  Be right back.
    "Ага.",                // 6  Yes.
    "Не-а.",               // 7  No.
    "Может.",              // 8  Maybe.
    "Жду у машины.",       // 9  Meet at the car.
    "Смотри в оба.",       // 10 Watch your back.
    "Камера слева.",       // 11 Camera on my left.
    "Тут камера Флок.",    // 12 Found a Flock cam.
    "Замри.",              // 13 Stay put.
    "Давай ко мне.",       // 14 Come to me.
    "Сваливаю.",           // 15 Leaving now.
    "Пять минут.",         // 16 Five minutes.
    "Опаздываю.",          // 17 Running late.
    "Ха.",                 // 18 Ha.
    "Найс.",               // 19 Nice.
    "Спасибо.",            // 20 Thanks.
    "Перекус?",            // 21 Snacks?
    "Хвост за тобой?",     // 22 Is it following you?
    "Ухожу в тень.",       // 23 Going dark.
    "Копы впереди.",       // 24 Cop car ahead.
    "Дрон сверху.",        // 25 Drone overhead.
    "Камера на номера.",   // 26 ALPR on the pole.
    "Их уже двое.",        // 27 Two of them now.
    "Уже ушли.",           // 28 It's gone now.
    "Я в порядке.",        // 29 I'm safe.
    "Сюда не иди.",        // 30 Don't come here.
    "Разворачивайся.",     // 31 Turn around.
    "За мной хвост.",      // 32 Being followed.
    "Ты в порядке?",       // 33 You okay?
    "Ты на месте?",        // 34 Still there?
    "Говорить можешь?",    // 35 Can you talk?
    "Куда дальше?",        // 36 Which way?
    "Сколько их?",         // 37 How many?
    "Подвезти?",           // 38 Need a ride?
    "Набери как сможешь.", // 39 Call when you can.
    "Принял.",             // 40 Got it.
    "Уже делаю.",          // 41 On it.
    "Пока нет.",           // 42 Not yet.
    "Вас понял.",          // 43 Copy that.
    "Сквач, отбой.",       // 44 Squatch out.
    "Мощно, если так.",    // 45 Big if true.
    "Бип-буп.",            // 46 Beep boop.
    "Будь сквачем.",       // 47 Stay squachy.
    "Понравилось.",        // 48 Liked it.
    "Не понравилось.",     // 49 Didn't like it.
};
const uint8_t CANNED_RU_N = (uint8_t)(sizeof(CANNED_RU) / sizeof(CANNED_RU[0]));
static_assert(sizeof(CANNED_RU) / sizeof(CANNED_RU[0]) == sizeof(CANNED) / sizeof(CANNED[0]),
              "CANNED_RU must mirror CANNED index for index");

const char* const CANNED_TAB_NAME_RU[CANNED_TABS] = {
    "ПУТЬ", "ВИЖУ", "СТАТУС", "ВОПРОС", "ОТВЕТ", "СКВАЧ",
};

// ---- the picker's tabs -------------------------------------------------------
// PRESENTATION ONLY. Unlike the indices above, nothing here goes on the air, so
// these may be reordered, renamed or regrouped in any release without breaking
// a single board. The layout exists because forty-eight lines paged four at a
// time is a worse way to find "Turn around." than six labelled tabs.
//
// The tabs are the same shape as the emote picker's, deliberately: both halves
// of the message screen then work the same way, and the one you learn first
// teaches the other.
const char* const CANNED_TAB_NAME[CANNED_TABS] = {
    "GOING", "SEEN", "SAFE", "ASK", "REPLY", "SQUACH",
};

// Each row is one tab: which CANNED indices it shows, in the order shown.
// 0xFF leaves a slot empty, which nothing uses yet but costs nothing to allow.
static const uint8_t CANNED_TAB[CANNED_TABS][CANNED_PER_TAB] = {
    // GOING -- movement and timing
    {  0,  4,  5,  9, 14, 15, 16, 17 },
    // SEEN -- what you spotted, which is what this device is for
    {  3, 11, 12, 24, 25, 26, 27, 28 },
    // SAFE -- status, and the lines that matter most if it goes wrong
    {  2, 10, 13, 23, 29, 30, 31, 32 },
    // ASK -- questions
    {  1, 22, 33, 34, 35, 36, 37, 38 },
    // REPLY -- short answers
    {  6,  7,  8, 20, 39, 40, 41, 42 },
    // SQUACH -- he is still a sasquatch about it
    { 18, 19, 21, 43, 44, 45, 46, 47 },
};

uint8_t cannedAtTab(uint8_t tab, uint8_t slot) {
    if (tab >= CANNED_TABS || slot >= CANNED_PER_TAB) return 0xFF;
    const uint8_t idx = CANNED_TAB[tab][slot];
    return (idx < CANNED_N) ? idx : 0xFF;
}

} // namespace MeshMsg
#endif // SQUACH_MESH
