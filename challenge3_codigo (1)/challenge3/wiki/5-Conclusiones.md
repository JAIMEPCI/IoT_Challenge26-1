# 5. Conclusiones, retos, trabajo futuro y referencias

## Conclusiones
<!-- 4-6 conclusiones con números de sus pruebas, una por objetivo específico. -->
1. ...

## Retos presentados
| Reto | Cómo se resolvió |
|---|---|
| Niveles lógicos 5 V / 3,3 V | Divisores resistivos |
| Ruido del ultrasonido | Mediana de 5 lecturas + persistencia de 15 s |
| BH1750 saturaba al sol | MTreg = 32 |
| ... | ... |

## Trabajo futuro
- Alimentación solar + batería y deep sleep.
- LoRaWAN para puntos sin WiFi.
- Integración con datos abiertos del IDEAM para pronóstico de lluvia.
- Modelo de predicción entrenado con varias semanas de datos (no solo regresión lineal).
- Varias cuentas con roles en la plataforma en la nube.

## Referencias
[1] Ministerio de Ambiente y Desarrollo Sostenible, "Gobierno confirma inicio del fenómeno de El Niño y alerta sobre su alcance," jun. 2026.
[2] Caracol Radio, "Fenómeno del Niño: los municipios en Cundinamarca con mayor riesgo de desabastecimiento de agua," 16 jun. 2026.
[3] Infobae, "Fenómeno de El Niño en Colombia: estos son los 16 escenarios de riesgo que podría vivir Bogotá," 8 jul. 2026.
[4] Gobernación de Cundinamarca – UAEGRD, "Plan de Contingencia para el Fenómeno de El Niño," 2026.
[5] R. G. Allen et al., "Crop evapotranspiration – FAO Irrigation and Drainage Paper 56," FAO, 1998.
[6] C. H. B. Priestley y R. J. Taylor, "On the assessment of surface heat flux and evaporation using large-scale parameters," Monthly Weather Review, 1972.
[7] OASIS, "MQTT Version 3.1.1," 2014.
[8] Hojas de datos: BME280 (Bosch), BH1750 (ROHM), DS3231 (Analog Devices), HC-SR04, ESP32 (Espressif).
[9] Documentación de Ubidots (MQTT API) y de la Gemini API (Google).
