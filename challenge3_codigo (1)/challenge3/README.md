# Monitoreo IoT de agua – Sabana Centro (Challenge #3 IoT, U. de La Sabana 2026-2)

ESP32 (sensores, fusión, alarma, tablero local) → MQTT → Raspberry Pi (Mosquitto, SQLite, IA) → MQTT/TLS → Ubidots.

## Inicio rápido

### Firmware (Arduino IDE)
1. Instalar el núcleo "esp32 by Espressif" y las bibliotecas: PubSubClient, ArduinoJson (v7), Adafruit SSD1306, Adafruit GFX, Adafruit BME280, Adafruit BMP280, BH1750 y DHT sensor library for ESPx.
2. `cp firmware/nodo_agua/config.example.h firmware/nodo_agua/config.h` y editar el hotspot, las claves y los sensores del kit (`SENSOR_CLIMA`, `SENSOR_LUZ`, `CAUDAL_REAL`).
3. Placa "ESP32 Dev Module" → Subir. Monitor serie a 115200 muestra la IP del tablero.
   Probado compilando con el núcleo 2.0.x; incluye la rama para 3.x.

### Wokwi
Crear un proyecto ESP32 en wokwi.com, pegar `nodo_agua.ino`, `config.h` (con `WOKWI 1`), `pagina.h`, `diagram.json` y `libraries.txt`.
El potenciómetro fija el caudal (el ESP32 genera los pulsos en GPIO2 → GPIO27 para que la ISR los cuente), el slider del HC-SR04 cambia el nivel.

### Gateway (Raspberry Pi OS)
Graba Raspberry Pi OS Lite (64 bits) con Raspberry Pi Imager: nombre `agua-gw`, usuario `pi`, WiFi del hotspot y SSH activado. Luego, desde tu PC:
```bash
ssh pi@agua-gw.local "mkdir -p ~/agua"
scp -r gateway pi@agua-gw.local:~/agua/
ssh pi@agua-gw.local
bash ~/agua/gateway/setup_rpi.sh      # instala Mosquitto, usuarios, ACL, Python y el servicio
```
Probar sin hardware: `~/agua/venv/bin/python ~/agua/gateway/simular_nodo.py --escenario sequia --user nodo01 --password <clave>`

### Análisis en tu PC
```bash
scp pi@agua-gw.local:~/agua/agua.db .
pip install -r gateway/requirements-pc.txt
python gateway/analisis.py --db agua.db --horas 3
```

### Wiki
Los archivos de `wiki/` se copian al repositorio de la wiki de GitHub (`<repo>.wiki.git`).
