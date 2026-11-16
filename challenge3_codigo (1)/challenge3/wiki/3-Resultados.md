# 3. Configuración experimental, resultados y análisis

## 3.1 Configuración experimental
<!-- Foto de la maqueta y del montaje. Describir: -->
- Tanque: balde de __ L, altura útil __ cm; sensor a __ cm del fondo; d_lleno = __ cm, d_vacío = __ cm.
- Consumo simulado: se saca agua del recipiente a mano (vaso o jeringa); el kit no incluye caudalímetro.
- Clima: BME280 y BH1750 en exterior/interior; para el escenario de calor se usó __ (secador/lámpara/sol directo).
- Red: WLAN __, Raspberry Pi __ con Mosquitto __, gateway Python __.
- Nube: Ubidots __ (STEM/Industrial), dispositivo `nodo01`.
- IA: modelo __ por la API REST de Gemini, periodo 10 min.

![Montaje](imagenes/montaje.jpg)

## 3.2 Resultados por prueba
<!-- Una subsección por cada prueba del protocolo, con captura y número. Ejemplos: -->

### P-01 Exactitud del nivel
| Altura real (cm) | Medida (cm) | Error (cm) |
|---|---|---|
| | | |

### P-02 Exactitud del caudal y volumen
| Volumen real (L) | Volumen medido (L) | Error (%) |
|---|---|---|
| | | |

### P-07 Escenario de sequía (gráfica generada con `analisis.py`)
![Serie de tiempo](imagenes/serie_tiempo.png)
- Tiempo hasta PRECAUCIÓN: __ s · hasta ALERTA: __ s · hasta CRÍTICO: __ s
- Coincidencia de alerta nodo vs gateway: __ %

### P-15 Latencia extremo a extremo
| Repetición | t sensor | t Ubidots | Latencia (s) |
|---|---|---|---|
| | | | |

### Capturas
- Tablero local (PC y celular) · Tablero Ubidots · Evento/notificación de Ubidots · LCD · Registro de la IA

## 3.3 Análisis
<!-- Interpreten: errores vs especificación del sensor, efecto de la persistencia (cuántas falsas alarmas se evitaron), latencia, % sincronizado tras la caída de Internet, utilidad y límites de la recomendación de IA. -->
