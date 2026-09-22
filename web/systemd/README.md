# Автозапуск browatch-web на хосте

Пакет: user-сервисы systemd + udev-правило. Ставится одним скриптом:

```sh
bash web/tools/web-install.sh
```

Что делает скрипт:

1. Копирует `web/` → `~/.local/share/browatch-web`.
2. Кладёт юниты в `~/.config/systemd/user`, `daemon-reload`,
   `enable --now browatch-web.service` (сервер на `127.0.0.1:40400`).
3. Включает linger (`loginctl enable-linger`), чтобы сервисы жили без сессии.
4. Пробует положить `99-browatch.rules` в `/etc/udev/rules.d`
   (нужен sudo; без него — печатает команды для ручного ввода).

После этого:

- сервер всегда поднят: `systemctl --user status browatch-web`;
- при подключении платы/донгла на CH340 (`1a86:7523`, проверьте своим
  `lsusb`) udev сам стартует `browatch-gateway@ttyUSB0.service`
  (платы — 2000000 бод; для ESP32-донгла поправьте baud через
  `systemctl --user edit browatch-gateway@ttyUSB0 --full` → 921600);
- вручную: `systemctl --user start browatch-gateway@ttyUSB0`
  (`stop`, `status`, логи — `journalctl --user -u browatch-web -f`).

Файлы: `browatch-web.service`, `browatch-gateway@.service`, `99-browatch.rules`.
