/**
 * @file config.h
 * @brief Configuración del nodo IoT de monitoreo hídrico (Challenge #3 IoT - U. de La Sabana).
 *
 * IMPORTANTE: este archivo tiene contraseñas. En GitHub suban solo
 * config.example.h y agreguen config.h al .gitignore.
 */
#pragma once

// ---------------------------------------------------------------------------
// Modo de compilación
// 1 = simulación en Wokwi (red Wokwi-GUEST y broker público)
// 0 = hardware real (hotspot del celular y Mosquitto en la Raspberry)
// ---------------------------------------------------------------------------
#define WOKWI 0

// ---------------------------------------------------------------------------
// Sensores que trae el kit: cambia SOLO estas tres líneas del bloque "#else"
// ---------------------------------------------------------------------------
#define CLIMA_BME280  1   // T, HR y presión (I2C)
#define CLIMA_BMP280  2   // T y presión, SIN humedad (I2C)
#define CLIMA_DHT22   3   // T y HR (1 cable, GPIO15); presión fija
#define CLIMA_DHT11   4   // T y HR (1 cable, GPIO15); presión fija
#define LUZ_BH1750    1   // luxómetro I2C
#define LUZ_LDR       2   // fotorresistencia o módulo LDR (AO a GPIO35)
#define LUZ_NINGUNO   0
#define CAUDAL_NINGUNO   0   // el kit no trae caudalímetro: se mide solo nivel
#define CAUDAL_YFS201    1   // caudalímetro de efecto Hall en PIN_CAUDAL
#define CAUDAL_SIMULADO  2   // potenciómetro en GPIO34 + puente GPIO2 -> PIN_CAUDAL
#define PANTALLA_LCD     1   // LCD 16x2 con módulo I2C (PCF8574)
#define PANTALLA_OLED    2   // OLED SSD1306 128x64 I2C
#if WOKWI
  #define SENSOR_CLIMA  CLIMA_DHT22
  #define SENSOR_LUZ    LUZ_LDR
  #define CAUDAL_MODO   CAUDAL_SIMULADO
  #define USAR_RTC      0
#else
  // Kit Aqua-Sabana: BME280 + BH1750 + HC-SR04 + DS3231 + LCD 16x2
  #define SENSOR_CLIMA  CLIMA_BME280
  #define SENSOR_LUZ    LUZ_BH1750
  #define CAUDAL_MODO   CAUDAL_NINGUNO
  #define USAR_RTC      1   // DS3231: hora de cada lectura aunque no haya Internet
#endif
#define PANTALLA        PANTALLA_LCD
#define LCD_ADDR        0x27  // si la LCD no muestra nada, prueba 0x3F
#define BUZZER_ACTIVO   1     // 1 = buzzer activo (suena con HIGH); 0 = pasivo (tono)
#define ZONA_HORARIA_S  (-5 * 3600)   // Colombia, UTC-5 (solo para mostrar la hora)
#define DEBUG_SERIAL    1   // imprime todas las lecturas cada 2 s (115200 baudios)

// ---------------------------------------------------------------------------
// Identidad y red
// ---------------------------------------------------------------------------
#define NODE_ID      "nodo01"
#define MDNS_NAME    "agua-nodo01"          // http://agua-nodo01.local

#if WOKWI
  #define WIFI_SSID  "Wokwi-GUEST"
  #define WIFI_PASS  ""
  #define MQTT_HOST  "broker.hivemq.com"    // broker público SOLO para simular
  #define MQTT_PORT  1883
  #define MQTT_USER  ""
  #define MQTT_PASS  ""
#else
  // Usa el hotspot de tu celular en 2,4 GHz (el ESP32 no ve redes de 5 GHz
  // ni redes universitarias con usuario y contraseña tipo WPA2-Enterprise)
  #define WIFI_SSID  "HOTSPOT_ANDRES"       // <-- cambiar
  #define WIFI_PASS  "clave_hotspot"        // <-- cambiar
  // Nombre de la Raspberry (se pone en Raspberry Pi Imager). Si no lo
  // encuentra, escribe aquí su IP: la ves con  hostname -I
  #define MQTT_HOST  "agua-gw.local"
  #define MQTT_PORT  1883
  #define MQTT_USER  "nodo01"               // usuario creado con mosquitto_passwd
  #define MQTT_PASS  "clave_nodo01"         // <-- cambiar
#endif

// Usuario del tablero web local (autenticación HTTP Digest)
#define WEB_USER     "autoridad"
#define WEB_PASS     "SabanaAgua2026"       // <-- cambiar

// ---------------------------------------------------------------------------
// Tópicos MQTT
// ---------------------------------------------------------------------------
#define TOPIC_TELEMETRIA "sabana/" NODE_ID "/telemetria"
#define TOPIC_ALERTA     "sabana/" NODE_ID "/alerta"
#define TOPIC_ESTADO     "sabana/" NODE_ID "/estado"
#define TOPIC_CMD        "sabana/" NODE_ID "/cmd"
#define TOPIC_IA         "sabana/" NODE_ID "/ia"

// ---------------------------------------------------------------------------
// Pines (ESP32 DevKit V1)
// ---------------------------------------------------------------------------
#define PIN_SDA        21
#define PIN_SCL        22
#define PIN_TRIG        5
#define PIN_ECHO       18   // HC-SR04 a 5 V: ECHO con divisor 1k/2k
#define PIN_LED_NORMAL 25   // LED azul: estado normal
#define PIN_LED_AMAR   26   // LED amarillo: precaución
#define PIN_LED_ROJO   27   // LED rojo: alerta (parpadea) / crítico (fijo)
#define PIN_BUZZER     23   // buzzer activo
#define PIN_BOTON      14   // pulsador opcional a GND (INPUT_PULLUP, interrupción)
#define PIN_CAUDAL     32   // caudalímetro (si CAUDAL_MODO lo usa; interrupción)
#define PIN_DHT        15   // DHT11/DHT22 (si el kit no trae BME280)
#define PIN_LDR        35   // AO del módulo LDR (si el kit no trae BH1750)
#define PIN_POT_CAUDAL 34   // potenciómetro del caudal simulado (CAUDAL_SIMULADO)
#define PIN_GEN_PULSOS  2   // genera pulsos: puente de GPIO2 a GPIO32 (CAUDAL_SIMULADO)

// ---------------------------------------------------------------------------
// Geometría del tanque (medir en la maqueta)
// ---------------------------------------------------------------------------
#define DIST_LLENO_CM  4.0f    // distancia sensor-agua con el recipiente lleno
#define DIST_VACIO_CM  25.0f   // distancia sensor-fondo con el recipiente vacío
#define DIST_MAX_CM    400.0f  // lecturas mayores se descartan

// ---------------------------------------------------------------------------
// Sensores
// ---------------------------------------------------------------------------
#define K_CAUDAL       7.5f      // YF-S201: f[Hz] = 7.5 * Q[L/min]
#define LUX_A_WM2      (1.0f/120.0f)  // aproximación luz solar
#define PRESION_FIJA   752.0f    // hPa típica en Chía (~2560 m s.n.m.); se usa
                                 // si el sensor de clima no mide presión
// Calibración del LDR real: lee "ldr_adc" en el monitor serie tapando el
// sensor con la mano (oscuro) y bajo la luz más fuerte que tengas (sol)
#define LDR_ADC_OSCURO   4000
#define LDR_ADC_SOL       300
#define LDR_WM2_SOL     1000.0f  // W/m2 que se asignan a la lectura "sol"

// ---------------------------------------------------------------------------
// Lógica de fusión (ver guía, sección 4)
// ---------------------------------------------------------------------------
#define UMBRAL_NIVEL_CRITICO   15.0f   // %
#define UMBRAL_NIVEL_BAJO      30.0f   // %
#define UMBRAL_NIVEL_MEDIO     50.0f   // %
#define UMBRAL_DESC_LEVE       -5.0f   // %/h
#define UMBRAL_DESC_FUERTE    -10.0f   // %/h
#define UMBRAL_TEMP_C          22.0f   // °C
#define UMBRAL_HUM_PCT         40.0f   // %
#define UMBRAL_EVAP_MMH         0.5f   // mm/h
#define UMBRAL_CAUDAL_CERO      0.2f   // L/min
#define PERSIST_SUBIR           3      // evaluaciones seguidas para subir
#define PERSIST_BAJAR           6      // evaluaciones seguidas para bajar
#define SILENCIO_MAX_MS   (30UL*60UL*1000UL)  // 30 min

// ---------------------------------------------------------------------------
// Tiempos
// ---------------------------------------------------------------------------
#define PERIODO_SENSADO_MS     1000
#define PERIODO_FUSION_MS      5000
#define PERIODO_PUBLICAR_MS    5000
#define PERIODO_HIST_MS       10000   // un punto de histórico cada 10 s
#define N_HIST                  120   // 120 x 10 s = 20 min en el tablero
#define N_TASA                   61   // 61 x 10 s = ventana de 10 min
