# BroWatch — вики форка

> BroWatch = русскоязычный форк SquachWatch-CYD (прошивка-детектор слежки + офлайн-мессенджер для ESP32-2432S028R / CYD).
> Апстрим: `skizzophrenic/SquachWatch-CYD` (на момент форка `v1.16.1`, GPL-3.0, сайт https://squachwatch.com/).
> Наш репо: `teletonn/BroWatch` (ветка `master`).

## Разделы

- [00 — Пошаговый план](00-PLAN.md) — исследование, решения, этапы, критерии готовности.
- [01 — Обзор и гайд](01-obzor.md) — что это, железо, детекции, BroMesh, прошивка и настройка.
- [02 — Русификация](02-rusifikaciya.md) — RU/EN режим, шрифт, `tr()/printRU`, что переведено, таблица шаблонов.
- [03 — Веб-мост](03-web-most.md) — шлюз USB/Serial + `browatch-web` на порту 40400.
- [04 — Android](04-android.md) — `browatch-app` (Native Kotlin + BLE).
- [05 — Ребренд](05-rebrand.md) — BroWatch/BroMesh: что переименовано, что оставлено для совместимости.
- [06 — Платы](06-platy.md) — инвентарь: какая плата каким env шьётся, MAC, шпаргалка по прошивке.

## Связанные репозитории

- `/run/media/al/ARCHIVE/code/browatch-web` — веб-приложение + шлюз (порт 40400).
- `/run/media/al/ARCHIVE/code/browatch-app` — Android-приложение (Kotlin, BLE).
- `/run/media/al/ARCHIVE/code/SquachWatch-CYD` — апстрим-зеркало с исходной RU-вики (`wiki/01-obzor-i-gayd.md`).

## Конвенции

- Язык вики — русский. Язык прошивки — переключаемый RU/EN (см. план, раздел 2).
- Все пути к коду даны от корня `BroWatch/` (например `src/detection.cpp`, `include/squachmesh.h`).
- Лицензия форка — GPL-3.0 (как `README.md` апстрима).
