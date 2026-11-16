# 6. Anexos

## Código fuente (carpeta del repositorio)
| Ruta | Descripción |
|---|---|
| `firmware/nodo_agua/nodo_agua.ino` | Firmware ESP32: ISR, tareas FreeRTOS, fusión, MQTT, web |
| `firmware/nodo_agua/pagina.h` | Tablero web local (HTML/JS sin dependencias externas) |
| `firmware/nodo_agua/config.example.h` | Configuración (copiar a `config.h`) |
| `firmware/wokwi/` | Simulación en Wokwi (`diagram.json`, `libraries.txt`) |
| `gateway/gateway.py` | Gateway: MQTT local, SQLite, Ubidots, comandos |
| `gateway/fusion.py` | Lógica de fusión (con pruebas: `python3 fusion.py`) |
| `gateway/ia_module.py` | Predicción local + recomendación con Gemini |
| `gateway/simular_nodo.py` | Simulador de nodo para pruebas |
| `gateway/analisis.py` | Exporta CSV y gráficas de resultados |
| `gateway/mosquitto/` | Configuración y ACL de Mosquitto |
| `gateway/systemd/` | Servicio para arrancar el gateway con la Raspberry |

## Esquemáticos
<!-- Archivos de KiCad/Fritzing y PDF. -->

## Simulación en Wokwi
Enlace al proyecto: [enlace]

## Datos de las pruebas
`resultados/lecturas.csv`, exportación de SQLite.
