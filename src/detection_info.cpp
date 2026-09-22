// SquachWatch-CYD — detection type explanations
#include "detection_info.h"
#include "detection.h"
#include "device_info.h"
#include "type_names.h"
#include "settings.h"
#include <stdio.h>

namespace DetectionInfo {

// Indexed by DetectionType -- keep in the exact same order as the enum
// in state.h (UNKNOWN..DEAUTH), one entry per value up to COUNT.
static const char* const EXPLAIN_TEXT[] = {
    // UNKNOWN
    "Nothing in the signature tables matched this one. It is the fallback the panel falls back TO, so if you are reading it about a real sighting, that sighting got logged under a type with no explanation of its own -- which is a bug worth reporting.",
    // FLOCK
    "Flock Safety makes automated license-plate-reader cameras, usually mounted on poles at neighborhood entrances. They log every plate that passes, suspect or not. Check the confidence: of the 29 hardware prefixes filed here exactly ONE is registered to Flock, and the rest are the generic Espressif and Liteon parts they build on -- shared with every dev board and smart plug on earth. A LOW reading here means a radio Flock might use, not a Flock camera.",
    // AXON
    "Axon makes body cameras and TASERs for law enforcement. This picks up a body cam's own wireless signal, not necessarily an officer's exact location.",
    // META
    "Camera glasses -- Ray-Ban Meta, Snap Spectacles and the like. They record video and photos, and the little LED that's supposed to warn you is easy to miss and easier to cover. Careful with this one: Meta puts the same Bluetooth ID on Quest headsets, so it can also just be a VR headset in a bag.",
    // SKIMMER
    "A Bluetooth card skimmer, usually wired into an ATM or gas pump reader. It quietly exfiltrates stolen card data over BLE instead of needing physical pickup.",
    // MESH
    "A mesh node talking off-grid: Meshtastic, MeshCore or Reticulum. Matched on Meshtastic's own Bluetooth ID, or the advertised name. Hikers and volunteers run these -- no towers needed.",
    // AIRTAG
    "An Apple AirTag, riding Apple's Find My network. Legitimate for keys and luggage -- also a known method for tracking a person or vehicle without consent.",
    // DRONE
    "A drone broadcasting Remote ID, the wireless 'license plate' the FAA requires most drones to transmit. It is decoded, not just spotted: where the aircraft is, and often where the person flying it is standing. Not its camera feed -- that stays private, which is rather the point of the complaint. Bluetooth Legacy only: this chip is BLE 4.2, so a drone using the Bluetooth 5 long-range form is invisible to it and always will be.",
    // ALPR
    "An automated license-plate reader from a vendor other than Flock. Same idea: logs every plate that passes, usually feeding a shared database.",
    // CAMERA
    "A camera-brand WiFi or Bluetooth radio, matched by hardware vendor rather than a specific known network. Could be a doorbell, a security cam, almost anything with a lens.",
    // SAMSUNG_TAG
    "A Samsung Galaxy SmartTag, riding Samsung's own item-finder network. Same tracking-without-consent concern as an AirTag, different ecosystem.",
    // GOOGLE_TAG
    "A tracker on Google's Find My Device network -- Chipolo, Pebblebee, Moto Tag and others all ride the same system. Android's answer to Find My.",
    // TILE
    "A Tile Bluetooth tracker, one of the original item-finders. Independent of Apple/Google/Samsung's networks, same basic capability either way.",
    // RING
    "A Ring doorbell or camera, Amazon's video doorbell line. Often networked into neighborhood-wide sharing through the Neighbors app.",
    // DEAUTH
    "Not a device -- a burst of WiFi deauthentication frames, the kind used to forcibly knock devices off a network. One frame is normal traffic; a flood like this usually isn't.",
    // EVILTWIN
    "Two different boxes are broadcasting the same network name, and they disagree about security -- one wants a password, the other is wide open. That's how a fake hotspot lures you on. A mesh system never argues with itself about encryption, which is what separates this from your own router.",
    // IBEACON
    "A proximity beacon, the kind bolted inside shops, stadiums and airports. It does not track you by itself -- it shouts an ID, and an app you already installed notices and reports where you are. The number shown is which deployment and which unit, so the same first half in two places is the same operator. This is the one detection that ships switched OFF, and about volume rather than importance: one shop can put more beacons in range than this device would otherwise see all week. Turn it on in DETECTION FILTER.",
    // HACKER
    "Wireless testing hardware: a Flipper Zero, a Pwnagotchi, a WiFi Pineapple or an ESP deauther. These are legitimate tools and most owners are hobbyists or people paid to break things -- but unlike everything else here, this is gear that transmits at other radios rather than just watching. A Pwnagotchi reports its own name and how many WiFi handshakes it has captured, because it is trying to be seen by others like it. Nothing here flags a bare ESP32 dev board: that would flag half the electronics in the room, and this detector too.",
};
static const uint8_t EXPLAIN_TEXT_N = sizeof(EXPLAIN_TEXT) / sizeof(EXPLAIN_TEXT[0]);

// Add a DetectionType without writing its MORE INFO paragraph and this
// fails the build. Without it explain() quietly clamps out-of-range to 0,
// so the new detection would show UNKNOWN's text -- wrong, plausible, and
// invisible until somebody happened to tap it.
static_assert(EXPLAIN_TEXT_N == (uint8_t)DetectionType::COUNT,
              "every DetectionType needs a MORE INFO entry, in enum order");

// BroWatch RU: the same paragraphs, adapted -- not transliterated. Russian
// needs fewer words for the same panel: seven lines of ~23 glyphs, under
// 320 bytes total (wrapTextRU's buffer), ASCII + Cyrillic only (no «»/--,
// the RU face has neither). Titles stay Latin product names on purpose.
static const char* const EXPLAIN_TEXT_RU[] = {
    // UNKNOWN
    "Совпадений в сигнатурах нет. Тип-заглушка: находка без описания. Видишь это про реальный сигнал - расскажи разработчику, это баг.",
    // FLOCK
    "Flock Safety: камеры-читалки номеров. Пишут все номера подряд. Из 29 префиксов Flock принадлежит лишь ОДИН, остальные - обычные чипы Espressif и Liteon. LOW - версия, не камера.",
    // AXON
    "Axon: нательные камеры и тейзеры полиции. Ловим собственный радиосигнал камеры, а не точное место офицера.",
    // META
    "Очки с камерой: Ray-Ban Meta, Snap Spectacles и подобные. Пишут видео и фото; светодиод легко не заметить или заклеить. Тот же Bluetooth-ID стоит на шлемах Quest - может, это VR в рюкзаке.",
    // SKIMMER
    "Bluetooth-скиммер: обычно впаян в банкомат или колонку АЗС. Тихо сливает краденые данные карт по BLE - забирать там нечего, подходить не надо.",
    // MESH
    "Узел меш-сети: Meshtastic, MeshCore или Reticulum. Ловим по Bluetooth-ID Meshtastic или имени узла. Туристы и волонтеры: связь без вышек и сети.",
    // AIRTAG
    "Apple AirTag в сети Локатора. Нормально для ключей и багажа - и известный способ следить за человеком или машиной без спроса.",
    // DRONE
    "Дрон с Remote ID - беспроводным номером от FAA. Декодировано: где борт, часто и где пилот. Не видеопоток - он закрыт. Только Bluetooth Legacy: чип BLE 4.2, Bluetooth 5 ему невидим.",
    // ALPR
    "Читалка номеров не от Flock. То же самое: пишет все номера подряд, обычно в общую базу.",
    // CAMERA
    "Радио камерного бренда по WiFi или Bluetooth - совпадение по производителю чипа, а не по конкретной сети. Может быть звонок, камера, что угодно с объективом.",
    // SAMSUNG_TAG
    "Samsung Galaxy SmartTag в собственной сети поиска Samsung. Та же беда со слежкой без спроса, что у AirTag, - другая экосистема.",
    // GOOGLE_TAG
    "Трекер в сети Find My Device от Google: Chipolo, Pebblebee, Moto Tag и другие сидят на одной системе. Ответ Android миру Find My.",
    // TILE
    "Tile - один из первых Bluetooth-трекеров. Вне сетей Apple, Google и Samsung, возможности те же.",
    // RING
    "Звонок или камера Ring - видеодомофоны Amazon. Часто сбиты в соседскую сеть через приложение Neighbors.",
    // DEAUTH
    "Не устройство, а пачка WiFi-кадров деаутентификации: так сгоняют устройства с сети. Один кадр - обычный трафик, flood - обычно нет.",
    // EVILTWIN
    "Два ящика с одним именем сети спорят о защите: один с паролем, другой открыт. Так заманивает поддельная точка. Домашний mesh сам с собой не спорит.",
    // IBEACON
    "Маяк близости: орет ID, а приложение докладывает, где ты. Сам не следит. Выключен из коробки - маяков слишком много. Включается в ФИЛЬТРЕ НАХОДОК.",
    // HACKER
    "Железо радиохакеров: Flipper Zero, Pwnagotchi, Pineapple, деаутентификатор. ПЕРЕДАЕТ в эфир, а не слушает - в этом отличие. Pwnagotchi орет имя и счет: по этому ловим.",
};
static const uint8_t EXPLAIN_TEXT_RU_N = sizeof(EXPLAIN_TEXT_RU) / sizeof(EXPLAIN_TEXT_RU[0]);
static_assert(EXPLAIN_TEXT_RU_N == (uint8_t)DetectionType::COUNT,
              "every DetectionType needs a RU MORE INFO entry, in enum order");

static inline bool isRU() { return Settings::lang() == 1; }

const char* explain(DetectionType t) {
    uint8_t idx = (uint8_t)t;
    if (isRU()) {
        if (idx >= EXPLAIN_TEXT_RU_N) idx = 0;
        return EXPLAIN_TEXT_RU[idx];
    }
    if (idx >= EXPLAIN_TEXT_N) idx = 0;
    return EXPLAIN_TEXT[idx];
}

const char* explainLive(DetectionType t, const DetectionEngine& eng) {
    if (t != DetectionType::DRONE) return explain(t);

    const RemoteId::Info& rid = eng.remoteId();
    if (!rid.haveBasic && !rid.haveLoc && !rid.haveOperator) return explain(t);

    // Replaces the paragraph rather than appending to it: Theme::wrapText
    // caps at 320 characters and the stock DRONE text already spends 185.
    // Only the parts actually received are printed -- the three messages
    // arrive as separate adverts, and 0,0 is a real place off Africa rather
    // than a safe stand-in for "not known yet".
    static char buf[320];
    int n = 0;
    const bool ru = isRU();
    if (rid.haveBasic)
        n += snprintf(buf + n, sizeof(buf) - n, "ID %s, %s. ",
                      rid.serial, RemoteId::uaTypeName(rid.uaType));
    if (rid.haveLoc && n < (int)sizeof(buf))
        n += snprintf(buf + n, sizeof(buf) - n,
                      ru ? "ДРОН %.5f, %.5f, высота %dm. "
                         : "AIRCRAFT %.5f, %.5f at %dm. ",
                      (double)rid.lat, (double)rid.lon, (int)rid.altM);
    if (rid.haveOperator && n < (int)sizeof(buf))
        n += snprintf(buf + n, sizeof(buf) - n,
                      ru ? "ОПЕРАТОР %.5f, %.5f - пилот стоит здесь."
                         : "OPERATOR %.5f, %.5f -- that is where the pilot is standing.",
                      (double)rid.opLat, (double)rid.opLon);
    buf[sizeof(buf) - 1] = '\0';
    return buf;
}

const char* explainFor(DetectionType t, const char* vendor, const char* name,
                       const DetectionEngine& eng) {
    if (t == DetectionType::DRONE) return explainLive(t, eng);
    const DeviceInfo::Device* d = DeviceInfo::find(t, vendor, name);
    if (!d) return explain(t);
    if (isRU() && d->textRU && d->textRU[0]) return d->textRU;
    return d->text;
}

const char* titleFor(DetectionType t, const char* vendor, const char* name) {
    const DeviceInfo::Device* d = DeviceInfo::find(t, vendor, name);
    // A device page keeps its product-name title (ASCII, Bangers);
    // the bare type falls back to the language-aware display name.
    return d ? d->title : TypeNames::display(t);
}

const char* rssiConfidencePrimer() {
    // Trimmed to fit the bigger text size this now renders at --
    // shorter sentences, same two facts.
    if (isRU())
        return "RSSI - сила сигнала в дБм: ближе к нулю - ближе, глубже в минус - дальше. "
               "Уверенность - насколько верим совпадению: HIGH - крепкое, MED и LOW - слабые догадки.";
    return "RSSI is signal strength in dBm -- closer to zero means closer, more negative means farther. "
           "Confidence is how sure the match is: HIGH is a strong match, MED/LOW are looser guesses.";
}

}
