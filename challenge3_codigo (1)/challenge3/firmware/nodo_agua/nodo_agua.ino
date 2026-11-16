/**
 * @file nodo_agua.ino
 * @brief Nodo IoT de monitoreo de disponibilidad de agua - Sabana Centro.
 *
 * Challenge #3 - Internet de las Cosas - Universidad de La Sabana 2026-2
 *
 * Arquitectura de ejecución (FreeRTOS, ESP32 de 2 núcleos):
 *   ISR  isrBoton()   -> marca la solicitud de silenciar la alarma
 *   ISR  isrCaudal()  -> cuenta pulsos del caudalímetro (si el kit lo trae)
 *   Tarea tareaSensado (núcleo 1) -> lee sensores, calcula variables y fusión
 *   Tarea tareaUI      (núcleo 1) -> LCD 16x2, LEDs y buzzer (notificación in situ)
 *   Tarea tareaMQTT    (núcleo 0) -> WiFi + MQTT hacia el gateway (Mosquitto)
 *   Tarea tareaWeb     (núcleo 0) -> tablero web local con autenticación Digest
 * El loop() de Arduino queda vacío: nada de la medición ni de la transmisión
 * corre en el hilo principal (requisito de la restricción técnica 1).
 *
 * Bibliotecas (Gestor de bibliotecas de Arduino):
 *   PubSubClient (Nick O'Leary), ArduinoJson v7 (Benoit Blanchon),
 *   LiquidCrystal I2C (Frank de Brabander), RTClib (Adafruit),
 *   Adafruit BME280, BH1750 (Christopher Laws),
 *   Wokwi: DHT sensor library for ESPx (beegee_tokyo)
 *   Opcionales: Adafruit BMP280, Adafruit SSD1306 + Adafruit GFX
 * Placa: "ESP32 Dev Module" (núcleo Arduino-ESP32 3.x).
 */
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <math.h>
#include <time.h>
#include "config.h"
#include "pagina.h"

#if PANTALLA == PANTALLA_LCD
  #include <LiquidCrystal_I2C.h>
  LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
#else
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  Adafruit_SSD1306 oled(128, 64, &Wire, -1);
#endif
#if USAR_RTC
  #include <RTClib.h>
  RTC_DS3231 rtc;
#endif
bool rtcOk = false;

#if SENSOR_CLIMA == CLIMA_BME280
  #include <Adafruit_BME280.h>
  Adafruit_BME280 bme;
#elif SENSOR_CLIMA == CLIMA_BMP280
  #include <Adafruit_BMP280.h>
  Adafruit_BMP280 bmp;
#else
  #include <DHTesp.h>
  DHTesp dht;
#endif
#if SENSOR_LUZ == LUZ_BH1750
  #include <BH1750.h>
  BH1750 luxometro;
#endif

// ===========================================================================
// Estado compartido entre tareas (protegido por mutex)
// ===========================================================================
enum Causa : uint32_t {
  C_NIVEL_MEDIO = 1 << 0,
  C_NIVEL_BAJO  = 1 << 1,
  C_NIVEL_CRIT  = 1 << 2,
  C_DESCENSO    = 1 << 3,
  C_DESC_FUERTE = 1 << 4,
  C_CALOR_SECO  = 1 << 5,
  C_EVAPORACION = 1 << 6,
  C_FUGA        = 1 << 7,
  C_SENSOR_ERR  = 1 << 8,
};

struct Estado {
  float distCm = NAN, nivelPct = NAN, tasaPctH = 0;
  float caudalLpm = 0, volumenL = 0;
  float tempC = NAN, humPct = NAN, presHpa = NAN, radWm2 = NAN, evapMmh = NAN;
  int   puntaje = 0;
  int   alerta = 0;            // 0 normal, 1 precaución, 2 alerta, 3 crítico
  uint32_t causas = 0;
  bool  silenciada = false;
  bool  mqttOk = false;
  char  iaTexto[200] = "Sin recomendacion aun";
};

struct PuntoHist { uint32_t t; float nivel, caudal, temp, hum, evap; int8_t alerta; };

Estado estado;
PuntoHist hist[N_HIST];
int histIdx = 0, histN = 0;
SemaphoreHandle_t mtx;      // protege "estado" e "hist"
SemaphoreHandle_t mtxI2C;   // un solo dueño del bus I2C a la vez (sensores, LCD, RTC)

const char* NOMBRE_ALERTA[] = {"NORMAL", "PRECAUCION", "ALERTA", "CRITICO"};

// ===========================================================================
// Interrupciones
// ===========================================================================
volatile uint32_t pulsosCaudal = 0;
volatile bool     solicitudSilencio = false;
volatile uint32_t ultimoBoton = 0;
portMUX_TYPE muxIsr = portMUX_INITIALIZER_UNLOCKED;

/** Cuenta un pulso del sensor de flujo (flanco de bajada). */
void IRAM_ATTR isrCaudal() {
  portENTER_CRITICAL_ISR(&muxIsr);
  pulsosCaudal++;
  portEXIT_CRITICAL_ISR(&muxIsr);
}

/** Pulsador de silencio con antirrebote de 250 ms. */
void IRAM_ATTR isrBoton() {
  uint32_t ahora = millis();
  if (ahora - ultimoBoton > 250) {
    solicitudSilencio = true;
    ultimoBoton = ahora;
  }
}

// ===========================================================================
// Utilidades
// ===========================================================================
WebServer server(80);
WiFiClient wifiCliente;
PubSubClient mqtt(wifiCliente);
uint32_t inicioSilencio = 0;

/** Silencia la alarma sonora; la alerta visual se mantiene. */
void silenciarAlarma(const char* origen) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (estado.alerta >= 2) {
    estado.silenciada = true;
    inicioSilencio = millis();
  }
  xSemaphoreGive(mtx);
  Serial.printf("[ALARMA] silenciada desde %s\n", origen);
}

/** Evaporación potencial horaria por Priestley-Taylor (mm/h). */
float evaporacionPT(float tC, float presHpa, float rsWm2) {
  if (isnan(tC) || isnan(presHpa) || isnan(rsWm2)) return NAN;
  float es    = 0.6108f * expf(17.27f * tC / (tC + 237.3f));
  float delta = 4098.0f * es / powf(tC + 237.3f, 2);
  float gamma = 0.000665f * (presHpa / 10.0f);       // hPa -> kPa
  float rn    = 0.77f * rsWm2 * 0.0036f;             // MJ/m2·h
  float ep    = 1.26f * delta / (delta + gamma) * rn / 2.45f;
  return ep < 0 ? 0 : ep;
}

/** Distancia por ultrasonido: mediana de 5 lecturas, NAN si falla. */
float leerDistanciaCm() {
  float v[5]; int n = 0;
  for (int i = 0; i < 5; i++) {
    digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    unsigned long dur = pulseIn(PIN_ECHO, HIGH, 30000UL);
    if (dur > 0) {
      float d = dur * 0.0343f / 2.0f;
      if (d > 2.0f && d < DIST_MAX_CM) v[n++] = d;
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  if (n == 0) return NAN;
  for (int i = 0; i < n - 1; i++)            // ordenamiento simple
    for (int j = i + 1; j < n; j++)
      if (v[j] < v[i]) { float t = v[i]; v[i] = v[j]; v[j] = t; }
  return v[n / 2];
}

/**
 * Lógica de fusión: combina nivel, tendencia, clima y caudal en un puntaje.
 * @return nivel de alerta candidato (0..3) antes del filtro temporal.
 */
int calcularAlertaCandidata(const Estado& e, int& puntaje, uint32_t& causas) {
  puntaje = 0; causas = 0;
  bool critico = false;

  if (isnan(e.nivelPct)) {
    causas |= C_SENSOR_ERR;
  } else {
    if (e.nivelPct < UMBRAL_NIVEL_CRITICO) { critico = true; causas |= C_NIVEL_CRIT; }
    if (e.nivelPct < UMBRAL_NIVEL_BAJO)       { puntaje += 2; causas |= C_NIVEL_BAJO; }
    else if (e.nivelPct < UMBRAL_NIVEL_MEDIO) { puntaje += 1; causas |= C_NIVEL_MEDIO; }

    if (e.tasaPctH < UMBRAL_DESC_FUERTE)    { puntaje += 2; causas |= C_DESC_FUERTE; }
    else if (e.tasaPctH < UMBRAL_DESC_LEVE) { puntaje += 1; causas |= C_DESCENSO; }

    if (!isnan(e.caudalLpm) && e.tasaPctH < UMBRAL_DESC_LEVE &&
        e.caudalLpm < UMBRAL_CAUDAL_CERO) {
      puntaje += 1; causas |= C_FUGA;     // baja el nivel sin consumo medido
    }
  }
  if (isnan(e.tempC) || isnan(e.humPct)) {
    causas |= C_SENSOR_ERR;
  } else if (e.tempC > UMBRAL_TEMP_C && e.humPct < UMBRAL_HUM_PCT) {
    puntaje += 1; causas |= C_CALOR_SECO;
  }
  if (!isnan(e.evapMmh) && e.evapMmh > UMBRAL_EVAP_MMH) {
    puntaje += 1; causas |= C_EVAPORACION;
  }

  if (critico || puntaje >= 6) return 3;
  if (puntaje >= 4) return 2;
  if (puntaje >= 2) return 1;
  return 0;
}

/** Texto legible de las causas (para web y MQTT). */
String textoCausas(uint32_t c) {
  String s;
  auto add = [&](const char* t) { if (s.length()) s += ", "; s += t; };
  if (c & C_NIVEL_CRIT)  add("nivel critico");
  else if (c & C_NIVEL_BAJO) add("nivel bajo");
  else if (c & C_NIVEL_MEDIO) add("nivel medio");
  if (c & C_DESC_FUERTE) add("descenso rapido");
  else if (c & C_DESCENSO) add("descenso");
  if (c & C_CALOR_SECO)  add("calor seco");
  if (c & C_EVAPORACION) add("evaporacion alta");
  if (c & C_FUGA)        add("posible fuga");
  if (c & C_SENSOR_ERR)  add("falla de sensor");
  if (!s.length()) s = "sin novedades";
  return s;
}

// ===========================================================================
// Tarea de sensado + fusión (núcleo 1)
// ===========================================================================
void tareaSensado(void*) {
  float histNivel[N_TASA]; int tasaIdx = 0, tasaN = 0;
  uint32_t tPrev = millis(), tFusion = 0, tHist = 0;
  int candidatoPrevio = 0, contSubir = 0, contBajar = 0;

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(PERIODO_SENSADO_MS));
    uint32_t ahora = millis();
    float dt = (ahora - tPrev) / 1000.0f; tPrev = ahora;

    // --- Caudal: se toman y reinician los pulsos contados por la ISR
    portENTER_CRITICAL(&muxIsr);
    uint32_t pulsos = pulsosCaudal; pulsosCaudal = 0;
    portEXIT_CRITICAL(&muxIsr);
#if CAUDAL_MODO == CAUDAL_NINGUNO
    float caudal = NAN;                        // el kit mide solo nivel
#else
    float caudal = (pulsos / dt) / K_CAUDAL;
#endif

    // --- Nivel
    float dist = leerDistanciaCm();
    float nivel = NAN;
    if (!isnan(dist)) {
      nivel = (DIST_VACIO_CM - dist) / (DIST_VACIO_CM - DIST_LLENO_CM) * 100.0f;
      nivel = constrain(nivel, 0.0f, 100.0f);
    }

    // --- Clima (el bus I2C se reserva mientras se lee)
    xSemaphoreTake(mtxI2C, portMAX_DELAY);
    float t, h, p, rad = NAN;
    int ldrAdc = -1;
#if SENSOR_CLIMA == CLIMA_BME280
    t = bme.readTemperature(); h = bme.readHumidity(); p = bme.readPressure() / 100.0f;
#elif SENSOR_CLIMA == CLIMA_BMP280
    t = bmp.readTemperature(); h = NAN; p = bmp.readPressure() / 100.0f;
#else
    TempAndHumidity th = dht.getTempAndHumidity();
    t = th.temperature; h = th.humidity; p = PRESION_FIJA;
#endif

    // --- Radiación solar
#if SENSOR_LUZ == LUZ_BH1750
    float lux = luxometro.readLightLevel();
    rad = lux < 0 ? NAN : lux * LUX_A_WM2;
#elif SENSOR_LUZ == LUZ_LDR
    ldrAdc = analogRead(PIN_LDR);
  #if WOKWI
    float v = ldrAdc / 4095.0f * 3.3f;                              // modelo Wokwi
    float r = 2000.0f * v / (1.0f - v / 3.3f + 1e-6f);
    rad = powf(50e3f * powf(10, 0.7f) / (r + 1e-6f), 1.0f / 0.7f) * LUX_A_WM2;
  #else
    rad = (ldrAdc - LDR_ADC_OSCURO) / (float)(LDR_ADC_SOL - LDR_ADC_OSCURO) * LDR_WM2_SOL;
    rad = constrain(rad, 0.0f, 1300.0f);                           // aproximación
  #endif
#endif

    xSemaphoreGive(mtxI2C);

#if CAUDAL_MODO == CAUDAL_SIMULADO
    // caudal simulado: potenciómetro 0..30 L/min -> pulsos reales hacia la ISR
    float qSim = analogRead(PIN_POT_CAUDAL) / 4095.0f * 30.0f;
    static uint32_t fPrev = UINT32_MAX;
    uint32_t f = (uint32_t)(qSim * K_CAUDAL);
    if (f < 2) f = 0;                          // LEDC no genera < ~1 Hz
    if (f != fPrev) {
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcWriteTone(PIN_GEN_PULSOS, f);       // núcleo 3.x: API por pin
  #else
      ledcWriteTone(2, f);                    // núcleo 2.x: canal 2 (tone() usa el 0)
  #endif
      fPrev = f;
    }
#endif
    if (t < -10 || t > 60) t = NAN;          // validación de rango
    if (h < 0 || h > 100)  h = NAN;
    if (p < 500 || p > 1100) p = NAN;

#if DEBUG_SERIAL
    static uint8_t nDebug = 0;
    if (++nDebug >= 2) {
      nDebug = 0;
      Serial.printf("[SENS] dist=%.1fcm nivel=%.1f%% pulsos=%u Q=%.2fL/min T=%.1fC HR=%.1f%% "
                    "P=%.1fhPa rad=%.0fW/m2 ldr_adc=%d alerta=%d\n",
                    dist, nivel, (unsigned)pulsos, caudal, t, h, p, rad, ldrAdc, estado.alerta);
    }
#endif

    // --- Tasa de cambio del nivel (ventana de 10 min)
    float tasa = estado.tasaPctH;
    if (ahora - tHist >= PERIODO_HIST_MS) {
      if (!isnan(nivel)) {
        histNivel[tasaIdx] = nivel;
        tasaIdx = (tasaIdx + 1) % N_TASA;
        if (tasaN < N_TASA) tasaN++;
        if (tasaN >= 7) {                      // al menos 1 minuto de datos
          int idxViejo = (tasaIdx - tasaN + N_TASA) % N_TASA;
          float minutos = (tasaN - 1) * PERIODO_HIST_MS / 60000.0f;
          tasa = (nivel - histNivel[idxViejo]) / minutos * 60.0f;
        }
      }
    }

    xSemaphoreTake(mtx, portMAX_DELAY);
    estado.distCm = dist;  estado.nivelPct = nivel; estado.tasaPctH = tasa;
    estado.caudalLpm = caudal;
    if (!isnan(caudal)) estado.volumenL += caudal / 60.0f * dt;
    estado.tempC = t; estado.humPct = h; estado.presHpa = p; estado.radWm2 = rad;
    estado.evapMmh = evaporacionPT(t, p, rad);

    // --- Histórico para el tablero local
    if (ahora - tHist >= PERIODO_HIST_MS) {
      tHist = ahora;
      hist[histIdx] = {ahora / 1000, nivel, caudal, t, h, estado.evapMmh, (int8_t)estado.alerta};
      histIdx = (histIdx + 1) % N_HIST;
      if (histN < N_HIST) histN++;
    }

    // --- Fusión con filtro temporal (persistencia + histéresis)
    if (ahora - tFusion >= PERIODO_FUSION_MS) {
      tFusion = ahora;
      int pts; uint32_t causas;
      int cand = calcularAlertaCandidata(estado, pts, causas);
      estado.puntaje = pts; estado.causas = causas;
      if (cand != candidatoPrevio) { contSubir = contBajar = 0; candidatoPrevio = cand; }
      if (cand > estado.alerta && ++contSubir >= PERSIST_SUBIR) {
        estado.alerta = cand; estado.silenciada = false;   // escaló: rearma
        contSubir = 0;
      } else if (cand < estado.alerta && ++contBajar >= PERSIST_BAJAR) {
        estado.alerta = cand; contBajar = 0;
        if (cand < 2) estado.silenciada = false;
      }
      if (estado.silenciada && millis() - inicioSilencio > SILENCIO_MAX_MS)
        estado.silenciada = false;
    }
    xSemaphoreGive(mtx);
  }
}

// ===========================================================================
// Tarea de notificación in situ: OLED, LEDs y buzzer (núcleo 1)
// ===========================================================================
/** Hora actual en segundos UTC desde el DS3231 (0 si no hay RTC). */
uint32_t horaRTC() {
#if USAR_RTC
  if (!rtcOk) return 0;
  xSemaphoreTake(mtxI2C, portMAX_DELAY);
  uint32_t t = rtc.now().unixtime();
  xSemaphoreGive(mtxI2C);
  return t;
#else
  return 0;
#endif
}

/** Escribe una línea completa de 16 caracteres (rellena con espacios, sin parpadeo). */
#if PANTALLA == PANTALLA_LCD
void lineaLCD(uint8_t fila, const char* txt) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", txt);
  lcd.setCursor(0, fila);
  lcd.print(buf);
}
#endif

/** Número con formato o "--" si el sensor no tiene dato. */
String num(float v, int dec) { return isnan(v) ? String("--") : String(v, dec); }

void tareaUI(void*) {
  bool buzzerOn = false; uint32_t tPantalla = 0; uint8_t pagina = 0, ciclos = 0;
  for (;;) {
    if (solicitudSilencio) { solicitudSilencio = false; silenciarAlarma("pulsador"); }

    xSemaphoreTake(mtx, portMAX_DELAY);
    Estado e = estado;
    xSemaphoreGive(mtx);

    // --- Semáforo y buzzer
    uint32_t ms = millis();
    bool parpadeo = (ms / 400) % 2;
    digitalWrite(PIN_LED_NORMAL, e.alerta == 0);
    digitalWrite(PIN_LED_AMAR,   e.alerta == 1);
    digitalWrite(PIN_LED_ROJO,   e.alerta == 3 || (e.alerta == 2 && parpadeo));

    bool sonar = !e.silenciada &&
                 (e.alerta == 3 || (e.alerta == 2 && (ms % 1000) < 300));
    if (sonar != buzzerOn) {
      buzzerOn = sonar;
#if BUZZER_ACTIVO
      digitalWrite(PIN_BUZZER, sonar ? HIGH : LOW);
#else
      if (sonar) tone(PIN_BUZZER, 2000); else noTone(PIN_BUZZER);
#endif
    }

    // --- Pantalla: se refresca cada 1 s; en la LCD rota 3 páginas cada 3 s
    if (ms - tPantalla >= 1000) {
      tPantalla = ms;
      if (++ciclos >= 3) { ciclos = 0; pagina = (pagina + 1) % 3; }
      char l0[40], l1[40];
      if (pagina == 0) {          // alerta + nivel / clima
        snprintf(l0, sizeof(l0), "%-10s%s%%", NOMBRE_ALERTA[e.alerta], num(e.nivelPct, 1).c_str());
        snprintf(l1, sizeof(l1), "T%s H%s P%s", num(e.tempC, 1).c_str(),
                 num(e.humPct, 0).c_str(), num(e.presHpa, 0).c_str());
      } else if (pagina == 1) {   // tendencia, radiación y evaporación
        snprintf(l0, sizeof(l0), "%s%%/h%s", num(e.tasaPctH, 1).c_str(), e.silenciada ? " SILENC" : "");
        snprintf(l1, sizeof(l1), "R%sW Ep%s", num(e.radWm2, 0).c_str(), num(e.evapMmh, 2).c_str());
      } else {                    // red y hora
        snprintf(l0, sizeof(l0), "%s", WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "Sin WiFi");
        uint32_t t = horaRTC();
        if (t) {
          time_t loc = (time_t)t + ZONA_HORARIA_S; struct tm tm; gmtime_r(&loc, &tm);
          snprintf(l1, sizeof(l1), "MQTT %s   %02d:%02d", e.mqttOk ? "ok" : "--", tm.tm_hour, tm.tm_min);
        } else {
          snprintf(l1, sizeof(l1), "MQTT %s", e.mqttOk ? "ok" : "--");
        }
      }
      xSemaphoreTake(mtxI2C, portMAX_DELAY);
#if PANTALLA == PANTALLA_LCD
      lineaLCD(0, l0); lineaLCD(1, l1);
#else
      oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE);
      oled.setCursor(0, 0);
      oled.printf("%s %s%s\n", NODE_ID, NOMBRE_ALERTA[e.alerta], e.silenciada ? " (S)" : "");
      oled.printf("Nivel %s%% %s%%/h\n", num(e.nivelPct, 1).c_str(), num(e.tasaPctH, 1).c_str());
      oled.printf("T %sC HR %s%%\n", num(e.tempC, 1).c_str(), num(e.humPct, 0).c_str());
      oled.printf("P %shPa R %sW\n", num(e.presHpa, 0).c_str(), num(e.radWm2, 0).c_str());
      oled.printf("Ep %s mm/h\n", num(e.evapMmh, 2).c_str());
      oled.printf("MQTT %s\n", e.mqttOk ? "ok" : "--");
      oled.print(WiFi.localIP());
      oled.display();
#endif
      xSemaphoreGive(mtxI2C);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ===========================================================================
// Tarea MQTT (núcleo 0)
// ===========================================================================
void alRecibirMqtt(char* topic, byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return;
  if (strcmp(topic, TOPIC_CMD) == 0) {
    if (doc["silenciar"] | false) silenciarAlarma("MQTT/Ubidots");
  } else if (strcmp(topic, TOPIC_IA) == 0) {
    const char* txt = doc["texto"] | "";
    xSemaphoreTake(mtx, portMAX_DELAY);
    strlcpy(estado.iaTexto, txt, sizeof(estado.iaTexto));
    xSemaphoreGive(mtx);
  }
}

volatile bool mdnsListo = false;

/** Traduce MQTT_HOST (IP, nombre .local o dominio) a una IP. */
bool resolverBroker(IPAddress& ip) {
  String h = MQTT_HOST;
  if (ip.fromString(h)) return true;
  if (h.endsWith(".local")) {
    String nombre = h.substring(0, h.length() - 6);
    ip = MDNS.queryHost((char*)nombre.c_str(), 3000);
    return ip[0] != 0;
  }
  return WiFi.hostByName(MQTT_HOST, ip) == 1;
}

void tareaMQTT(void*) {
  mqtt.setCallback(alRecibirMqtt);
  mqtt.setBufferSize(1024);
  uint32_t tPub = 0, tReintento = 0; int ultimaAlerta = -1;
  bool ultimaSilenciada = false, brokerOk = false;
  IPAddress brokerIP;

  for (;;) {
    if (WiFi.status() == WL_CONNECTED && !mdnsListo) {
      MDNS.begin(MDNS_NAME);
      mdnsListo = true;
      configTime(0, 0, "pool.ntp.org", "time.google.com");   // hora UTC por Internet
      Serial.printf("[WiFi] conectado, IP del nodo: %s\n", WiFi.localIP().toString().c_str());
    }
#if USAR_RTC
    // Ajusta el DS3231 una vez con la hora de Internet; después funciona sin red
    static bool rtcAjustado = false;
    if (rtcOk && !rtcAjustado && time(nullptr) > 1700000000) {
      xSemaphoreTake(mtxI2C, portMAX_DELAY);
      rtc.adjust(DateTime((uint32_t)time(nullptr)));
      xSemaphoreGive(mtxI2C);
      rtcAjustado = true;
      Serial.println("[RTC] hora ajustada por NTP");
    }
#endif
    if (WiFi.status() != WL_CONNECTED) {
      if (millis() - tReintento > 10000) { tReintento = millis(); WiFi.reconnect(); }
    } else if (!mqtt.connected()) {
      if (millis() - tReintento > 5000) {      // un intento cada 5 s
        tReintento = millis();
        if (!brokerOk) {
          brokerOk = resolverBroker(brokerIP);
          if (brokerOk) {
            mqtt.setServer(brokerIP, MQTT_PORT);
            Serial.printf("[MQTT] broker %s = %s\n", MQTT_HOST, brokerIP.toString().c_str());
          } else {
            Serial.printf("[MQTT] no encuentro %s: escribe la IP de la Raspberry en MQTT_HOST\n", MQTT_HOST);
          }
        }
        if (brokerOk) {
          String id = String(NODE_ID) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
          if (mqtt.connect(id.c_str(), MQTT_USER, MQTT_PASS, TOPIC_ESTADO, 1, true, "offline")) {
            mqtt.publish(TOPIC_ESTADO, "online", true);
            mqtt.subscribe(TOPIC_CMD, 1);
            mqtt.subscribe(TOPIC_IA, 1);
            Serial.println("[MQTT] conectado");
          } else {
            Serial.printf("[MQTT] fallo rc=%d (rc=5: usuario o clave MQTT incorrectos)\n", mqtt.state());
            brokerOk = false;                  // la IP pudo cambiar: volver a buscarla
          }
        }
      }
    } else {
      mqtt.loop();
    }

    xSemaphoreTake(mtx, portMAX_DELAY);
    estado.mqttOk = mqtt.connected();
    Estado e = estado;
    xSemaphoreGive(mtx);

    if (mqtt.connected() && millis() - tPub >= PERIODO_PUBLICAR_MS) {
      tPub = millis();
      JsonDocument d;
      d["node"] = NODE_ID;
      d["uptime_s"] = millis() / 1000;
      if (!isnan(e.nivelPct)) { d["nivel_pct"] = e.nivelPct; d["dist_cm"] = e.distCm; }
      d["tasa_pct_h"] = e.tasaPctH;
      if (!isnan(e.caudalLpm)) { d["caudal_lpm"] = e.caudalLpm; d["volumen_l"] = e.volumenL; }
      uint32_t ts = horaRTC();
      if (ts > 1700000000) d["ts"] = ts;       // hora de la medición (RTC, UTC)
      if (!isnan(e.tempC))   d["temp_c"]   = e.tempC;
      if (!isnan(e.humPct))  d["hum_pct"]  = e.humPct;
      if (!isnan(e.presHpa)) d["pres_hpa"] = e.presHpa;
      if (!isnan(e.radWm2))  d["rad_wm2"]  = e.radWm2;
      if (!isnan(e.evapMmh)) d["evap_mmh"] = e.evapMmh;
      d["alerta"] = e.alerta; d["puntaje"] = e.puntaje;
      d["causas"] = e.causas; d["silenciada"] = e.silenciada;
      char buf[512]; size_t n = serializeJson(d, buf);
      mqtt.publish(TOPIC_TELEMETRIA, (uint8_t*)buf, n, false);
    }
    if (mqtt.connected() && (e.alerta != ultimaAlerta || e.silenciada != ultimaSilenciada)) {
      JsonDocument d;
      d["node"] = NODE_ID; d["alerta"] = e.alerta; d["nombre"] = NOMBRE_ALERTA[e.alerta];
      d["causas"] = textoCausas(e.causas); d["silenciada"] = e.silenciada;
      char buf[256]; size_t n = serializeJson(d, buf);
      if (mqtt.publish(TOPIC_ALERTA, (uint8_t*)buf, n, true)) {
        ultimaAlerta = e.alerta; ultimaSilenciada = e.silenciada;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ===========================================================================
// Tablero web local (núcleo 0) - acceso restringido con HTTP Digest
// ===========================================================================
bool autorizado() {
  if (!server.authenticate(WEB_USER, WEB_PASS)) {
    server.requestAuthentication(DIGEST_AUTH, "Tablero Agua Sabana", "Acceso restringido");
    return false;
  }
  return true;
}

void apiEstado() {
  if (!autorizado()) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  Estado e = estado;
  xSemaphoreGive(mtx);
  JsonDocument d;
  d["node"] = NODE_ID;
  d["nivel_pct"] = e.nivelPct; d["tasa_pct_h"] = e.tasaPctH;
  d["caudal_lpm"] = e.caudalLpm; d["volumen_l"] = e.volumenL;
  d["temp_c"] = e.tempC; d["hum_pct"] = e.humPct; d["pres_hpa"] = e.presHpa;
  d["rad_wm2"] = e.radWm2; d["evap_mmh"] = e.evapMmh;
  d["alerta"] = e.alerta; d["nombre"] = NOMBRE_ALERTA[e.alerta];
  d["puntaje"] = e.puntaje; d["causas"] = textoCausas(e.causas);
  d["silenciada"] = e.silenciada; d["mqtt"] = e.mqttOk; d["ia"] = e.iaTexto;
  d["uptime_s"] = millis() / 1000;
  String out; serializeJson(d, out);           // NAN se serializa como null
  server.send(200, "application/json", out);
}

void apiHistoria() {
  if (!autorizado()) return;
  static PuntoHist copia[N_HIST]; int n, idx;
  xSemaphoreTake(mtx, portMAX_DELAY);
  memcpy(copia, hist, sizeof(hist)); n = histN; idx = histIdx;
  xSemaphoreGive(mtx);
  uint32_t ahora = millis() / 1000;
  JsonDocument d;
  JsonArray t = d["t"].to<JsonArray>(), nv = d["nivel"].to<JsonArray>(),
            q = d["caudal"].to<JsonArray>(), te = d["temp"].to<JsonArray>(),
            hu = d["hum"].to<JsonArray>(), ev = d["evap"].to<JsonArray>(),
            al = d["alerta"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    const PuntoHist& p = copia[(idx - n + i + N_HIST) % N_HIST];
    t.add((int32_t)p.t - (int32_t)ahora);      // segundos relativos (negativos)
    nv.add(p.nivel); q.add(p.caudal); te.add(p.temp);
    hu.add(p.hum); ev.add(p.evap); al.add(p.alerta);
  }
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

void tareaWeb(void*) {
  while (!mdnsListo) vTaskDelay(pdMS_TO_TICKS(500));
  server.on("/", HTTP_GET, [] { if (autorizado()) server.send(200, "text/html", PAGINA_HTML); });
  server.on("/api/estado", HTTP_GET, apiEstado);
  server.on("/api/historia", HTTP_GET, apiHistoria);
  server.on("/api/silenciar", HTTP_POST, [] {
    if (!autorizado()) return;
    silenciarAlarma("tablero local");
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/logout", HTTP_GET, [] {
    server.requestAuthentication(DIGEST_AUTH, "Tablero Agua Sabana", "Sesion cerrada");
  });
  server.onNotFound([] { server.send(404, "text/plain", "No encontrado"); });
  server.begin();
  Serial.printf("[WEB] http://%s/  o  http://%s.local/\n",
                WiFi.localIP().toString().c_str(), MDNS_NAME);
  for (;;) { server.handleClient(); vTaskDelay(pdMS_TO_TICKS(2)); }
}

// ===========================================================================
// setup / loop
// ===========================================================================
void setup() {
  Serial.begin(115200);
  mtx = xSemaphoreCreateMutex();
  mtxI2C = xSemaphoreCreateMutex();

  pinMode(PIN_TRIG, OUTPUT); pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_NORMAL, OUTPUT); pinMode(PIN_LED_AMAR, OUTPUT); pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BOTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BOTON), isrBoton, FALLING);
#if CAUDAL_MODO != CAUDAL_NINGUNO
  pinMode(PIN_CAUDAL, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_CAUDAL), isrCaudal, FALLING);
#endif

  Wire.begin(PIN_SDA, PIN_SCL);
  // Escaneo I2C: muestra qué dispositivos responden (útil al cablear)
  Serial.println("[I2C] buscando (LCD 0x27/0x3F, BME280 0x76, BH1750 0x23, DS3231 0x68 + EEPROM 0x57)...");
  int nI2c = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("[I2C]   encontrado 0x%02X\n", a); nI2c++; }
  }
  if (!nI2c) Serial.println("[I2C]   ninguno: revisa SDA=21, SCL=22, 3V3 y GND");
#if PANTALLA == PANTALLA_LCD
  lcd.init();
  lcd.backlight();
  lineaLCD(0, "Aqua-Sabana"); lineaLCD(1, "Iniciando...");
#else
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("[OLED] no encontrada");
  oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0); oled.println("Aqua-Sabana"); oled.println("Iniciando..."); oled.display();
#endif
#if USAR_RTC
  rtcOk = rtc.begin();
  if (!rtcOk) Serial.println("[RTC] DS3231 no encontrado");
  else if (rtc.lostPower()) Serial.println("[RTC] sin hora: se ajusta sola al tener Internet");
#endif

#if SENSOR_CLIMA == CLIMA_BME280
  if (!bme.begin(0x76) && !bme.begin(0x77)) Serial.println("[BME280] no encontrado: si el chip dice BMP280 cambia SENSOR_CLIMA");
#elif SENSOR_CLIMA == CLIMA_BMP280
  if (!bmp.begin(0x76) && !bmp.begin(0x77)) Serial.println("[BMP280] no encontrado");
#elif SENSOR_CLIMA == CLIMA_DHT11
  dht.setup(PIN_DHT, DHTesp::DHT11);
#else
  dht.setup(PIN_DHT, DHTesp::DHT22);
#endif
#if SENSOR_LUZ == LUZ_BH1750
  if (!luxometro.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) Serial.println("[BH1750] no encontrado");
  luxometro.setMTreg(32);                  // menos sensibilidad: no satura con sol directo
#endif
#if CAUDAL_MODO == CAUDAL_SIMULADO
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_GEN_PULSOS, 1000, 10);    // generador de pulsos de caudal simulado
  #else
  ledcSetup(2, 1000, 10); ledcAttachPin(PIN_GEN_PULSOS, 2);
  #endif
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  xTaskCreatePinnedToCore(tareaSensado, "sensado", 6144, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(tareaUI,      "ui",      4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(tareaMQTT,    "mqtt",    8192, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(tareaWeb,     "web",     8192, nullptr, 1, nullptr, 0);
}

void loop() {
  vTaskDelete(nullptr);   // todo corre en tareas FreeRTOS
}
