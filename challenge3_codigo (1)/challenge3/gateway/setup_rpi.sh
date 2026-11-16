#!/usr/bin/env bash
# setup_rpi.sh - Instala todo el gateway en la Raspberry Pi en un solo paso.
#
# Antes: copia la carpeta "gateway" a la Raspberry, en ~/agua/gateway
#   ssh pi@agua-gw.local "mkdir -p ~/agua"
#   scp -r gateway pi@agua-gw.local:~/agua/
# Luego, conectado por SSH a la Raspberry:
#   bash ~/agua/gateway/setup_rpi.sh
#
# Usa claves solo con letras y números (sin espacios ni símbolos).
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"     # carpeta gateway
BASE="$(dirname "$DIR")"                 # ~/agua
USUARIO="$(whoami)"
paso() { echo; echo "==> $*"; }

read -rp "Clave MQTT del usuario 'nodo01' (la MISMA de MQTT_PASS en config.h): " CLAVE_NODO
read -rp "Clave MQTT del usuario 'gateway' (inventa una): " CLAVE_GW
read -rp "Token de Ubidots (Enter para dejarlo vacío por ahora): " TOKEN

paso "1/6 Instalando Mosquitto, SQLite y Python (necesita Internet, 3-8 min)"
sudo apt-get update -y
sudo apt-get install -y mosquitto mosquitto-clients python3-venv sqlite3

paso "2/6 Configurando Mosquitto con usuarios y ACL"
# Configuraciones de sesiones anteriores pueden abrir otro listener en 1883 y
# chocar con esta: se guardan como .bak en lugar de borrarlas
for f in /etc/mosquitto/conf.d/*.conf; do
  [ -e "$f" ] && [ "$(basename "$f")" != "agua.conf" ] && sudo mv "$f" "$f.bak" && echo "  respaldado: $f.bak"
done
sudo cp "$DIR/mosquitto/agua.conf" /etc/mosquitto/conf.d/agua.conf
sudo cp "$DIR/mosquitto/acl" /etc/mosquitto/acl
sudo rm -f /etc/mosquitto/passwd
sudo touch /etc/mosquitto/passwd
sudo mosquitto_passwd -b /etc/mosquitto/passwd gateway "$CLAVE_GW"
sudo mosquitto_passwd -b /etc/mosquitto/passwd nodo01 "$CLAVE_NODO"
sudo chown mosquitto:mosquitto /etc/mosquitto/passwd /etc/mosquitto/acl
sudo chmod 600 /etc/mosquitto/passwd /etc/mosquitto/acl
sudo systemctl enable mosquitto
sudo systemctl restart mosquitto
sleep 1
systemctl is-active --quiet mosquitto || { echo "Mosquitto no arrancó: sudo journalctl -u mosquitto -n 30"; exit 1; }

paso "3/6 Entorno de Python"
python3 -m venv "$BASE/venv"
"$BASE/venv/bin/pip" install -q -r "$DIR/requirements.txt"

paso "4/6 Archivo config.env"
[ -f "$DIR/config.env" ] || cp "$DIR/config.example.env" "$DIR/config.env"
sed -i "s|^MQTT_LOCAL_PASS=.*|MQTT_LOCAL_PASS=$CLAVE_GW|; s|^DB_PATH=.*|DB_PATH=$BASE/agua.db|" "$DIR/config.env"
if [ -n "$TOKEN" ]; then
  sed -i "s|^UBIDOTS_TOKEN=.*|UBIDOTS_TOKEN=$TOKEN|" "$DIR/config.env"
elif grep -q "^UBIDOTS_TOKEN=BBUS-x" "$DIR/config.env"; then
  sed -i "s|^UBIDOTS_TOKEN=.*|UBIDOTS_TOKEN=|" "$DIR/config.env"   # sin nube por ahora
fi

paso "5/6 Probando la lógica de fusión"
"$BASE/venv/bin/python" "$DIR/fusion.py"

paso "6/6 Servicio para que el gateway arranque solo al prender la Raspberry"
TMP_SERV="$(mktemp)"
sed -e "s|^User=.*|User=$USUARIO|" -e "s|/home/pi/agua|$BASE|g" \
  "$DIR/systemd/agua-gateway.service" > "$TMP_SERV"
sudo cp "$TMP_SERV" /etc/systemd/system/agua-gateway.service
rm -f "$TMP_SERV"
sudo systemctl daemon-reload
sudo systemctl enable agua-gateway
sudo systemctl restart agua-gateway
sleep 3
systemctl --no-pager --lines=5 status agua-gateway || true

echo
echo "================ LISTO ================"
echo "IP de esta Raspberry : $(hostname -I)"
echo "Nombre en la red     : $(hostname).local"
echo "Ver mensajes del nodo: mosquitto_sub -u gateway -P '$CLAVE_GW' -t 'sabana/#' -v"
echo "Registros del gateway: journalctl -u agua-gateway -f"
echo "Probar sin el ESP32  : $BASE/venv/bin/python $DIR/simular_nodo.py --user nodo01 --password '$CLAVE_NODO'"
