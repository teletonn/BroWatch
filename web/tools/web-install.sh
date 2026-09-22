#!/usr/bin/env bash
# Установка веб-приложения BroWatch + автозапуск (user-сервисы systemd + udev).
# Запуск из корня репо:  bash web/tools/web-install.sh
set -eu

REPO_WEB="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$HOME/.local/share/browatch-web"
UNIT_DIR="$HOME/.config/systemd/user"

echo "== копирую $REPO_WEB -> $DEST"
mkdir -p "$DEST"
cp "$REPO_WEB/server.py" "$REPO_WEB/gateway.py" "$REPO_WEB/smoke.py" \
   "$REPO_WEB/API.md" "$REPO_WEB/README.md" "$REPO_WEB/VERSION" "$DEST/"
rm -rf "$DEST/static" "$DEST/tools"
cp -r "$REPO_WEB/static" "$REPO_WEB/tools" "$DEST/"
rm -rf "$DEST/tools/__pycache__"
chmod +x "$DEST/server.py" "$DEST/gateway.py"

echo "== юниты -> $UNIT_DIR"
mkdir -p "$UNIT_DIR"
sed "s|%h|$HOME|g" "$REPO_WEB/systemd/browatch-web.service" > "$UNIT_DIR/browatch-web.service"
sed "s|%h|$HOME|g" "$REPO_WEB/systemd/browatch-gateway@.service" > "$UNIT_DIR/browatch-gateway@.service"

systemctl --user daemon-reload
systemctl --user enable --now browatch-web.service
loginctl enable-linger "$USER" 2>/dev/null || true

echo "== udev-правило для автостарта шлюза при подключении платы"
if [ -w /etc/udev/rules.d ]; then
    cp "$REPO_WEB/systemd/99-browatch.rules" /etc/udev/rules.d/
    udevadm control --reload-rules
    echo "правило установлено"
else
    echo "нужен sudo для /etc/udev/rules.d:"
    echo "  sudo cp $REPO_WEB/systemd/99-browatch.rules /etc/udev/rules.d/"
    echo "  sudo udevadm control --reload-rules"
fi

echo "== проверка"
sleep 1
curl --max-time 5 -s http://127.0.0.1:40400/api/health; echo
echo "готово: откройте http://127.0.0.1:40400"
echo "шлюз вручную: systemctl --user start browatch-gateway@ttyUSB0"
