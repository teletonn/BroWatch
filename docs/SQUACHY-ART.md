# Squachy: как он нарисован, анимирован и переодет

Исследование кодовой базы (октябрь 2026). Только факты, с точками входа.

## 1. Коротко

- Персонаж **на 100% процедурный**: ни одного PNG/BMP/XBM, ни одного
  `drawBitmap`/`pushImage` для тела. Только примитивы TFT_eSPI:
  `fillCircle` / `fillRoundRect` / `fillTriangle` / `fillEllipse` /
  `drawLine` + своя толстая линия `wideLine()` (`src/squachy.cpp:66-82`,
  два треугольника + два круга: штатные `drawWideLine` давали ~28 мс
  и были выкинуты ради скорости).
- Весь персонаж живёт в одном файле **`src/squachy.cpp` (~6950 строк)**:
  машина настроений + `tick()` + `drawBody()` + `drawOutfit()` +
  пулы реплик, рост, очки, костюмы, NVS, визиты/эмоуты.
- Система координат — масштабируемые единицы `S(v) = v*scale`
  (`auto S` в каждой функции рисования). Масштаб считается в `tick()`
  от доступной высоты; камео принимают готовый `scale`. Всё,
  нарисованное через `S()`, безопасно при любом размере экрана.
- «Скин» как таковой **не выбирается**: `SKIN_TAN`/`SKIN_DARK`
  (`include/theme.h:39-43`) — это фиксированная краска морды и ушей.
  То, что выглядит как «скин», — три независимых слоя:
  **тинт очков** (4 цвета) + **перекрас меха ростом** (4 стадии) +
  **костюмы** (15 штук, геометрия + палитра).

## 2. Файлы

| Файл | Строк | Роль |
|---|---|---|
| `src/squachy.cpp` | ~6950 | Всё: `Mood`, `tick()`, `drawBody()`, `drawOutfit()`, очки, рост, NVS |
| `include/squachy.h` | ~540 | Публичный API: `tick()`, `drawWaving()`, `VisitPose`, `trigger()`, превью |
| `include/squachmesh.h` / `src/squachmesh.cpp` | ~100/~130 | Сетевой протокол внешности: `Peer{nick,outfit,shade}`, `SQM1`, слово внешности 16 бит |
| `src/theme.cpp` | ~9490 | Тело Сквочи НЕ рисует. Фоны, `drawLilGuy()`, `drawYeti()`, `drawWing()` (крыло CHROME WING), палитры |
| `include/theme.h:39-43` | — | `FUR_DARK/FUR_MAIN/FUR_LIGHT`, `SKIN_TAN/SKIN_DARK` — базовые краски |
| `src/ui_clear.cpp` | ~3570 | Хост `tick()` + толпа/визиты через `drawWaving()` с превью чужой внешности |
| `src/ui_squad.cpp`, `src/ui_desk.cpp` | — | Карусель отряда / портрет входящего — тот же паттерн превью |
| `src/emote_script.cpp` | ~570 | Дата-драйвен эмоуты: `Pose/Fx/Obj/Beat/Script`, играют через `VisitPose` |
| `src/mesh.cpp:122-201` | — | Свои `outfit/shade/nick` уходят в эфир; чужие складываются в отряд |
| `src/bw_bridge.cpp:106-157` | — | USB→web зеркало: внешность каждого пира едет своим набором |
| `include/lil_guy.h` | 39 | Единственный настоящий битмап-спрайт в проекте: `LILGUY[8*10]`, 10×10 2 бит |
| `sim/` | — | Эмулятор: `squachsim` (рендер в PNG), `squachsim-live` (интерактив), `squachgui` (галерея в браузере), wasm для web-flasher |

Проверка «есть ли битмапы»: поиск `PROGMEM|XBM|RLE|drawBitmap|pushImage`
по `src/` + `include/` даёт только шрифты (`bangers`, `ru`, `glcd`),
таблицы звёзд `VOID_*` и `LILGUY`. Тела Сквочи среди них нет.

## 3. Точки входа рисования

- `drawBody(t, cx, hy, headTopY, now, m, scale, …)` — `src/squachy.cpp:4777`,
  ~1200 строк: тинт меха → крылья/хвост → ноги/торс/руки → голова/морда →
  `drawOutfit()` → цилиндр → пропсы → неймтег.
- `drawOutfit(t, cx2, hy, now, m, scale, outfit)` — `:3927`, `switch(outfit)`
  с векторным кодом каждого костюма; `NONE` — ранний выход.
- `drawWaving(…)` — `:5990` (+ `squachy.h:536`): stateless-камео для
  гостей/толпы/превью. Маппит `VisitPose → Mood` (`:6064-6087`), зовёт
  `drawBody(..., ownsBubble=false)` (`:6088`), потом `drawBubble` (`:6108`).
  Покачивание: `bob 6.0*scale @900мс`, смех `7.0 @240мс`, старTL `-4+jitter`.
- `tick(…)` — `:6183` (+ `squachy.h:452`): драйвер главного экрана CLEAR.
  Шоу-офф `:6199` — 16 поз подряд (`IDLE+SQUASH, WAVE, BOUNCE, WINK,
  STRETCH+YAWN, GUM, JUGGLE, DANCE, WALK×3, DOUBLE-TAKE, BINOCULARS, DUCK,
  PICK UP+DROP, NAP`); дальше по `advance` — настроения, реплики (`say()`),
  ходьба, конфетти.

## 4. Анимация: кадров нет, всё параметрическое

- Состояния: `enum class Mood` (`:84-90`):
  `IDLE, WAVE, SHOCKED, BOUNCE, SLEEPY, WALK, DANCE, WINK, STRETCH, GUM,
  JUGGLE, HIGHFIVE, PUMP, ACT` + `ReactPose` (`:118`) для реакций на типы
  детекций (`reactPoseFor()`, `:120-136`).
- Сетевые позы гостей: `VisitPose` (`include/squachy.h:518`, ~26 штук:
  `HIGH_FIVE, DANCE, LAUGH, SALUTE, BOW, HUG, SAD, GRR, CROUCH, SELFIE,
  HOWL, …`); хост играет их через `Mood::ACT + s_actPose`.
- Время: всё через `tempo(ms)` (`:18`, дефолт 70, крутится настройкой
  `PACE`), базы: шаг ходьбы 9000 мс (`:832`), потягушка 1900 (`:1007`),
  шок 1400, баунс 1200–2000. `moodUntil/nextIdleAt/bubbleUntil` гаснут в IDLE.
- Как меняется поза (всё `sin(now)` / счётчики, кадров нет):
  - приседание/растяжка: `s_headDrop` (`:988`), `crouch/actHead` (`:4787-4796`, `CROUCH +S(9)`);
  - руки: `s_armL0/L1/R0/R1` (`:1049-1050`) пишут `limbTo()/restArms()`, читают настроения (`:5332-5535`) и костюмы (рукава PARKA `:4034-4054`);
  - глаза: очки-рамки + блик `WHITE` (`2400мс`, `:5829`), моргание по хешу (`слот 2600`, `:5781-5795`), `WINK` закрывает левый, `SHOCKED` — белые эллипсы, `SLEEPY` — чёрточки + `Z`;
  - рот: болтовня `(now/160)%2` (`:5864`), `HOWL` — эллипс, `GUM` — надувающийся/хлопающий пузырь (`:5906-5936`), `JUGGLE` — 3 пакета по синусоидам (`:5941-5965`), покой — улыбка.
- Новая анимация = новый `Mood` (или ветка `VisitPose`+`ACT`) +
  `say(реплика, мс)` + `moodUntil = now+tempo(X)` + ветки рук/головы/рта/глаз
  в `drawBody` + слот в `SHOW_STEP` (`:6212-6242`). Кейфрейм-редактора нет —
  только ручные синусы/лерпы.

## 5. Слои «скина»

1. **Тинт очков** (ближе всего к «скину»): `SHADE_NAMES = CYAN, PINK,
   GREEN, PURPLE` (`:1183`), `SHADE_TINTS` (`:5761`), хранится в NVS
   (`shadeIdx`), листается `cycleShadesColor()` (`:2271`), доступность —
   `unlockedShadeCount()` (`:1186`: поглаживания ≥10/25/50 открывают 2/3/4).
2. **Перекрас ростом**: `GrowthStage{FLEDGLING, TRACKER≥25, VETERAN≥100,
   LEGEND≥500}` (`:1362-1369`, счётчик — lifetime-детекции). В `drawBody`
   (`:4806-4819`) мех блендится к циану/белому/янтарю, у LEGEND — радуга
   (`:4860`) + цилиндр (`hasTopHat()`, `:4762/5583`).
3. **Костюмы**: `OutfitId` (`:1199`) — `NONE, TANOOKI, UNICORN` (бесплатно),
   `TINFOIL(5), SHADOW(15), PLUMBER(25), TALLBRO(40), SPACE(60),
   BLUEBLUR(100), CAPTAIN(150)` (за счётчик), `WOLFPELT, CHROMEWING,
   VOIDEYE` (за события в фонах), `PARKA, SHARK`. Хранятся в NVS
   (`outfitIdx` + флаги событий, `:1565-1580`). Часть костюмов целиком
   перекрашивает мех (`UNICORN/BLUEBLUR/SHADOW/VOIDEYE/PARKA`, `:4827-4855`),
   все добавляют геометрию в `drawOutfit`.
4. **Имя**: 10 ников (`:1175`) + своё (`cname`, 12 символов).

Сеть: слово внешности 16 бит (`include/squachmesh.h:60-73`:
`nick 4 бита / outfit 4 бита / shade 2 бита / custom 1 бит`).
«Гость в своём скине» — везде один паттерн: `setOutfitPreview(p.outfit);
setShadesPreview(p.shade); setNameTag(…); drawWaving(…); clear()` —
превью-оверрайды (`s_outfitOverride/s_shadeOverride`), не копии
(`ui_clear.cpp:2000-2026, 2994-3046`; `ui_squad.cpp:164-165`;
`ui_desk.cpp:344-348`; web — `bw_bridge.cpp:147-153`).

## 6. Инструменты предпросмотра (sim/)

- `squachsim <экран> out.png [--bg N --theme N --frames N --outfit N --pose N …]` —
  рендерит настоящий `squachy.cpp`/`theme.cpp` в PNG (`--frames 90` на прогрев).
- `squachsim-live` — интерактивное устройство в терминале.
- `squachgui.bat` / Gallery — то же в браузере (`localhost:842`), галерея + Save PNG.
- `sim/test_outfit_poses.py` — каждый костюм × 27 поз × 8 фаз, ловит вылезшие пиксели.
- `sim/make_gallery.py → docs/outfits.png` — витрина всех костюмов; `render_all.sh` —
  все `docs/*.gif` из эмулятора (реальные кадры прошивки).

## 7. Что легко / что трудно (для художника)

**Легко (только числа):**
- Новый тинт очков: имя в `SHADE_NAMES` (`:1183`) + цвет в `SHADE_TINTS`
  (`:5761`) + `SHADE_N` (`src/squachmesh.cpp:25`); хранится и едет по сети само.
- Пропорции/цвета базы: торс `torsoHalf()` (`:64`), голова `S(30)×S(24)`
  (`:5602`), морда `S(18)×S(11)` (`:5604`), чуб (`:5637-5650`), уши/румянец
  (`:5662-5669`), краски `theme.h:39-43`. Всё в `S()` — масштаб не ломается.
- Костюм-палитра без геометрии: новый `OutfitId` + запись в `OUTFITS{name,
  threshold}` (`:1220`) + ветка `furMain/furLight` (`:4826-4855`).

**Трудно (геометрия/анимация/растр):**
- Новая геометрия: новый `case` в `drawOutfit` (`:3933`) в пространстве
  `cx2/hy/S()`; надо учесть руки всех настроений, приседания, `hideFace/
  noMouth`, двойной проход отрисовки (состояние — только `pure now`,
  см. комментарий про мигание `:5771-5774`); валидация —
  `test_outfit_poses.py` + `squachsim poses --outfit N`.
- Новая анимация: новый `Mood` + планировщик в `tick` + ветки в `drawBody` +
  слот шоу-оффа; если поза сетевая — ещё `VisitPose`, `emote_script.h`,
  версия `squachmesh`, карта гостей в `ui_clear`.
- **Растрового пайплайна нет вообще.** Хочешь пиксель-арт по референсу —
  его придётся построить: ручная упаковка в `PROGMEM` + свой блит +
  квантование (см. заметку RGB332 `theme.h:46-62`), учёт флеша/RAM.
  Единственный образец — `LILGUY` 10×10 2bpp (`include/lil_guy.h`).

**Рецепт нового «скина» (5-й тинт, напр. AMBER):**
`src/squachy.cpp:1183 SHADE_NAMES`, `:5761 SHADE_TINTS`,
`:1186 unlockedShadeCount`, `src/squachmesh.cpp:25 SHADE_N`
(+ биты в `include/squachmesh.h:61-72`, если больше 2 бит под shade),
сеть/мост подхватят сами (`mesh.cpp:129`, `bw_bridge.cpp:121,151`),
смотреть через `squachsim clear --frames 90` или Gallery в `squachgui`.
