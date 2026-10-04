#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <BH1750.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <time.h>
 
// =========================================================================
// CONFIGURACIÓN DE RED Y CREDENCIALES
// =========================================================================
const char* ssid = "FAMILIA_OLARTE";
const char* password = "jaime2022";
const char* admin_user = "jajajaime";
const char* admin_pass = "jejejeime";
 
// Pines de Actuadores y Sensores
const int buzzerPin = 23;
const int trigPin = 5;
const int echoPin = 18;
 
// Pines LEDs (Indicadores de estado)
const int ledAzul = 25;      // Normal
const int ledAmarillo = 26;  // Alerta Media
const int ledRojo = 27;      // Alerta Crítica
 
// Objetos I2C (BME280, BH1750 y LCD comparten el mismo bus SDA 21 / SCL 22)
LiquidCrystal_I2C lcd(0x27, 16, 2);
BH1750 sensorLuz;
Adafruit_BME280 bme;
 
WebServer server(80);
 
// =========================================================================
// PARÁMETROS DE CALIBRACIÓN - LÓGICA DE FUSIÓN
// =========================================================================
const float ALTURA_TANQUE_CM = 20.0;   // Calibrar con la altura real del balde/tanque
 
const int   NIVEL_CRITICO_PORC = 20;
const int   NIVEL_MEDIO_PORC   = 40;
const float TEMP_ALTA_C        = 28.0;
const float LUZ_ALTA_LUX       = 1000;
const float HUMEDAD_BAJA_PORC  = 65.0; // Por debajo del rango normal IDEAM (73-86%)
 
// =========================================================================
// VARIABLES GLOBALES (Lecturas)
// =========================================================================
float real_distancia = 0.0;
float real_temperatura = 0.0;
float real_humedad = 0.0;
float real_presion = 0.0;
float real_luz = 0.0;
float nivel_agua_porc = 0.0;
 
bool buzzer_silenciado = false;
bool hay_peligro = false;
int nivel_alerta = 0; // 0=Normal, 1=Media, 2=Crítica
 
// =========================================================================
// ESTRUCTURA DEL HISTÓRICO
// =========================================================================
struct RegistroDatos {
  String timestamp;
  float nivel;
  float nivelPorc;
  float temp;
  float hum;
  float luz;
  float pres;
  int alerta;
};
 
const int MAX_HISTORICO = 60;
RegistroDatos historico[MAX_HISTORICO];
int indice_historico = 0;
int cantidad_historico = 0;
 
unsigned long ultimo_guardado = 0;
const unsigned long INTERVALO_GUARDADO = 5000; // Configurable
 
// =========================================================================
// FUNCIÓN PARA OBTENER FECHA Y HORA
// =========================================================================
String obtenerFechaHora() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Obteniendo hora...";
  }
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}
 
// =========================================================================
// LÓGICA DE FUSIÓN DE DATOS (clase LogicaFusion del diseño UML)
// Combina nivel de agua + temperatura + humedad relativa + luz.
// El riesgo evaporativo real requiere temperatura o luz alta JUNTO CON
// humedad baja (relación física documentada: ETP directa con temp/luz,
// inversa con humedad — ver Anexos/Referencias Académicas).
// =========================================================================
void calcularLogicaFusion() {
  nivel_agua_porc = ((ALTURA_TANQUE_CM - real_distancia) / ALTURA_TANQUE_CM) * 100.0;
  nivel_agua_porc = constrain(nivel_agua_porc, 0.0, 100.0);
 
  bool nivelCritico = nivel_agua_porc <= NIVEL_CRITICO_PORC;
  bool nivelMedio    = nivel_agua_porc <= NIVEL_MEDIO_PORC;
  bool tempAlta       = real_temperatura >= TEMP_ALTA_C;
  bool humedadBaja     = real_humedad <= HUMEDAD_BAJA_PORC;
  bool luzAlta           = real_luz >= LUZ_ALTA_LUX;
 
  // Riesgo evaporativo real: temperatura o luz alta + humedad baja a la vez
  bool riesgoEvaporativoAlto = (tempAlta || luzAlta) && humedadBaja;
 
  if (nivelCritico && riesgoEvaporativoAlto) {
    nivel_alerta = 2; // CRÍTICA: nivel bajo + condición real de evaporación acelerada
  } else if (nivelMedio || nivelCritico || riesgoEvaporativoAlto) {
    nivel_alerta = 1; // MEDIA: nivel bajo/medio, o alerta temprana por riesgo evaporativo
  } else {
    nivel_alerta = 0; // NORMAL
  }
 
  hay_peligro = (nivel_alerta == 2);
  if (nivel_alerta != 2) {
    buzzer_silenciado = false;
  }
}
 
// =========================================================================
// TAREA FREERTOS: SENSORES, LCD Y ALARMA
// =========================================================================
void TareaSensoresYAlarma(void *parameter) {
  int ciclo_lcd = 0;
 
  for (;;) {
    // 1. LEER SENSOR HC-SR04
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    long duracion = pulseIn(echoPin, HIGH);
    real_distancia = (duracion * 0.0343) / 2.0;
 
    // 2. LEER SENSORES I2C
    real_temperatura = bme.readTemperature();
    real_humedad = bme.readHumidity();
    real_presion = bme.readPressure() / 100.0F;
    real_luz = sensorLuz.readLightLevel();
 
    // 3. LÓGICA DE FUSIÓN
    calcularLogicaFusion();
 
    // CONTROL EXCLUSIVO DE LEDs
    if (nivel_alerta == 0) {
      digitalWrite(ledAzul, HIGH); digitalWrite(ledAmarillo, LOW); digitalWrite(ledRojo, LOW);
    } else if (nivel_alerta == 1) {
      digitalWrite(ledAzul, LOW); digitalWrite(ledAmarillo, HIGH); digitalWrite(ledRojo, LOW);
    } else if (nivel_alerta == 2) {
      digitalWrite(ledAzul, LOW); digitalWrite(ledAmarillo, LOW); digitalWrite(ledRojo, HIGH);
    }
 
    // 4. CONTROL DEL BUZZER (solo suena en alerta CRÍTICA)
    if (hay_peligro && !buzzer_silenciado) digitalWrite(buzzerPin, HIGH);
    else digitalWrite(buzzerPin, LOW);
 
    // 5. GUARDAR EN EL HISTÓRICO
    if (millis() - ultimo_guardado >= INTERVALO_GUARDADO || ultimo_guardado == 0) {
      historico[indice_historico].timestamp = obtenerFechaHora();
      historico[indice_historico].nivel = real_distancia;
      historico[indice_historico].nivelPorc = nivel_agua_porc;
      historico[indice_historico].temp = real_temperatura;
      historico[indice_historico].hum = real_humedad;
      historico[indice_historico].luz = real_luz;
      historico[indice_historico].pres = real_presion;
      historico[indice_historico].alerta = nivel_alerta;
 
      indice_historico = (indice_historico + 1) % MAX_HISTORICO;
      if (cantidad_historico < MAX_HISTORICO) cantidad_historico++;
 
      ultimo_guardado = millis();
    }
 
    // 6. ACTUALIZAR PANTALLA LCD
    lcd.clear();
    if (nivel_alerta == 2) {
      lcd.setCursor(0, 0); lcd.print("! ALERTA MAXIMA !");
      lcd.setCursor(0, 1); lcd.print("Nivel: " + String(nivel_agua_porc, 0) + " %");
    } else if (nivel_alerta == 1) {
      lcd.setCursor(0, 0); lcd.print("Alerta Media");
      lcd.setCursor(0, 1); lcd.print("Nivel: " + String(nivel_agua_porc, 0) + " %");
    } else {
      int pantalla = (ciclo_lcd / 3) % 3;
      if (pantalla == 0) {
        lcd.setCursor(0, 0); lcd.print("Temp: " + String(real_temperatura, 1) + " C");
        lcd.setCursor(0, 1); lcd.print("Hum : " + String(real_humedad, 1) + " %");
      } else if (pantalla == 1) {
        lcd.setCursor(0, 0); lcd.print("Luz: " + String(real_luz, 0) + " lx");
        lcd.setCursor(0, 1); lcd.print("Pre: " + String(real_presion, 1) + " hPa");
      } else {
        lcd.setCursor(0, 0); lcd.print("Nivel agua:");
        lcd.setCursor(0, 1); lcd.print(String(nivel_agua_porc, 0) + " % (" + String(real_distancia, 1) + "cm)");
      }
    }
 
    ciclo_lcd++;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
 
// =========================================================================
// PÁGINA WEB (HTML + CSS + JavaScript)
// =========================================================================
const char* paginaHTML = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Tablero de Control IoT - Sabana Centro</title>
  <style>
    :root { --primary: #0275d8; --danger: #d9534f; --warning: #f0ad4e; --dark: #292b2c; --bg: #f7f9fb; }
    body { font-family: 'Segoe UI', Roboto, sans-serif; background-color: var(--bg); color: var(--dark); margin: 0; padding: 20px; transition: background-color 0.5s ease; }
    .header { text-align: center; margin-bottom: 20px; }
    .header h1 { color: var(--primary); margin: 0; font-size: 2.5em; text-transform: uppercase; letter-spacing: 2px;}
    .header p { color: #777; font-size: 1.1em; margin-top: 5px; }
    .estado-banner { text-align: center; max-width: 600px; margin: 0 auto 25px auto; padding: 14px; border-radius: 10px; font-size: 1.3em; font-weight: bold; color: white; background-color: var(--primary); transition: background-color 0.4s ease; }
    .estado-banner.media { background-color: var(--warning); }
    .estado-banner.critica { background-color: var(--danger); animation: parpadeo 1.5s infinite alternate; }
    .grid-container { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; max-width: 1200px; margin: 0 auto; }
    .card { background: white; padding: 25px 15px; border-radius: 12px; box-shadow: 0 6px 12px rgba(0,0,0,0.05); text-align: center; border-top: 5px solid var(--primary); transition: all 0.3s ease; }
    .etiqueta { font-size: 1em; color: #888; font-weight: bold; text-transform: uppercase; }
    .valor { font-size: 3em; font-weight: 900; margin: 10px 0; color: #444; }
    .unidad { font-size: 0.4em; color: #999; vertical-align: middle; }
    .peligro-bg { background-color: #ffe6e6 !important; }
    .peligro-card { border-top-color: var(--danger) !important; animation: parpadeo 1.5s infinite alternate; }
    .media-card { border-top-color: var(--warning) !important; }
    .peligro-text { color: var(--danger) !important; }
    @keyframes parpadeo { from { box-shadow: 0 4px 6px rgba(217,83,79,0.1); } to { box-shadow: 0 4px 15px rgba(217,83,79,0.6); transform: scale(1.02); } }
    .btn-container { text-align: center; margin-top: 40px; margin-bottom: 40px;}
    button { background-color: var(--danger); color: white; border: none; padding: 18px 40px; font-size: 1.5em; font-weight: bold; border-radius: 50px; cursor: pointer; display: none; box-shadow: 0 5px 15px rgba(217,83,79,0.4); transition: background-color 0.3s, transform 0.2s; }
    button:hover { background-color: #c9302c; transform: translateY(-2px); }
    button.silenciado { background-color: #5cb85c; box-shadow: 0 5px 15px rgba(92,184,92,0.4); }
    .tabla-container { grid-column: 1 / -1; text-align: left; overflow-x: auto; }
    table { width: 100%; border-collapse: collapse; margin-top: 15px; font-size: 0.9em; }
    th, td { padding: 12px 15px; text-align: center; border-bottom: 1px solid #ddd; }
    th { background-color: var(--primary); color: white; font-weight: bold; text-transform: uppercase;}
    tr:nth-child(even) { background-color: #f9f9f9; }
    tr:hover { background-color: #f1f1f1; }
    .tabla-titulo { text-align: center; color: var(--primary); font-size: 1.8em; margin-bottom: 10px; }
  </style>
</head>
<body id="cuerpo">
  <div class="header">
    <h1>Vigilancia Hidrica Reales</h1>
    <p>Red de Monitoreo - Sabana Centro (Sensores Fisicos)</p>
  </div>
  <div class="estado-banner" id="banner_estado">Cargando estado...</div>
  <div class="grid-container">
    <div class="card" id="card_nivel">
      <div class="etiqueta">Nivel de Agua</div>
      <div class="valor" id="val_nivel">--<span class="unidad">%</span></div>
    </div>
    <div class="card" id="card_temp">
      <div class="etiqueta">Temperatura</div>
      <div class="valor" id="val_temp">--<span class="unidad">C</span></div>
    </div>
    <div class="card" id="card_humedad">
      <div class="etiqueta">Humedad Relativa</div>
      <div class="valor" id="val_humedad">--<span class="unidad">%</span></div>
    </div>
    <div class="card" id="card_rad">
      <div class="etiqueta">Iluminacion</div>
      <div class="valor" id="val_rad">--<span class="unidad">Lux</span></div>
    </div>
    <div class="card">
      <div class="etiqueta">Presion Atmosferica</div>
      <div class="valor" id="val_pres">--<span class="unidad">hPa</span></div>
    </div>
    <div class="card tabla-container">
      <h2 class="tabla-titulo">Historial de Mediciones</h2>
      <table>
        <thead>
          <tr>
            <th>Fecha y Hora</th><th>Nivel (%)</th><th>Temp (C)</th>
            <th>Humedad (%)</th><th>Luz (Lux)</th><th>Presion (hPa)</th><th>Alerta</th>
          </tr>
        </thead>
        <tbody id="cuerpo_tabla"><tr><td colspan="7">Cargando datos...</td></tr></tbody>
      </table>
    </div>
  </div>
  <div class="btn-container">
    <button id="btn_silenciar" onclick="silenciarAlarma()">DESACTIVAR ALARMA</button>
  </div>
  <script>
    const NOMBRES_ALERTA = ["NORMAL", "ALERTA MEDIA", "ALERTA CRITICA"];
    setInterval(function() {
      fetch('/datos').then(response => response.json()).then(data => {
        document.getElementById('val_nivel').innerHTML = data.nivelPorc.toFixed(0) + '<span class="unidad">%</span>';
        document.getElementById('val_temp').innerHTML = data.temp.toFixed(1) + '<span class="unidad">C</span>';
        document.getElementById('val_humedad').innerHTML = data.hum.toFixed(1) + '<span class="unidad">%</span>';
        document.getElementById('val_rad').innerHTML = data.luz.toFixed(0) + '<span class="unidad">Lux</span>';
        document.getElementById('val_pres').innerHTML = data.pres.toFixed(1) + '<span class="unidad">hPa</span>';
        let banner = document.getElementById('banner_estado');
        banner.innerText = NOMBRES_ALERTA[data.nivel_alerta];
        banner.classList.remove('media', 'critica');
        document.getElementById('cuerpo').classList.remove('peligro-bg');
        document.getElementById('card_nivel').classList.remove('peligro-card', 'media-card');
        document.getElementById('card_temp').classList.remove('peligro-card', 'media-card');
        document.getElementById('val_nivel').classList.remove('peligro-text');
        document.getElementById('val_temp').classList.remove('peligro-text');
        document.getElementById('btn_silenciar').style.display = 'none';
        if (data.nivel_alerta === 1) {
          banner.classList.add('media');
          document.getElementById('card_nivel').classList.add('media-card');
        } else if (data.nivel_alerta === 2) {
          banner.classList.add('critica');
          document.getElementById('cuerpo').classList.add('peligro-bg');
          document.getElementById('card_nivel').classList.add('peligro-card');
          document.getElementById('card_temp').classList.add('peligro-card');
          document.getElementById('val_nivel').classList.add('peligro-text');
          document.getElementById('val_temp').classList.add('peligro-text');
          if(!data.silenciado) {
             document.getElementById('btn_silenciar').style.display = 'inline-block';
             document.getElementById('btn_silenciar').innerText = 'DESACTIVAR ALARMA';
             document.getElementById('btn_silenciar').classList.remove('silenciado');
          }
        }
      });
    }, 1000);
    function actualizarHistorico() {
      fetch('/historico').then(response => response.json()).then(data => {
        let tbody = document.getElementById('cuerpo_tabla');
        tbody.innerHTML = '';
        if(data.length === 0) { tbody.innerHTML = '<tr><td colspan="7">Aun no hay datos guardados.</td></tr>'; return; }
        data.forEach(row => {
          tbody.innerHTML += `<tr><td><strong>${row.time}</strong></td><td>${row.nivelPorc}</td><td>${row.temp}</td><td>${row.hum}</td><td>${row.luz}</td><td>${row.pres}</td><td>${NOMBRES_ALERTA[row.alerta]}</td></tr>`;
        });
      });
    }
    setInterval(actualizarHistorico, 10000);
    actualizarHistorico();
    function silenciarAlarma() {
      fetch('/silenciar', { method: 'POST' }).then(response => {
        if(response.ok) {
          let btn = document.getElementById('btn_silenciar');
          btn.innerText = "ALARMA SILENCIADA";
          btn.classList.add('silenciado');
          setTimeout(() => { btn.style.display = 'none'; }, 3000);
        }
      });
    }
  </script>
</body>
</html>
)rawliteral";
 
// =========================================================================
// MANEJADORES DE RUTAS DEL SERVIDOR WEB
// =========================================================================
void manejarRaiz() {
  if (!server.authenticate(admin_user, admin_pass)) return server.requestAuthentication();
  server.send(200, "text/html", paginaHTML);
}
 
void manejarDatos() {
  if (!server.authenticate(admin_user, admin_pass)) return server.requestAuthentication();
  String json = "{";
  json += "\"nivel\":" + String(real_distancia) + ",";
  json += "\"nivelPorc\":" + String(nivel_agua_porc) + ",";
  json += "\"temp\":" + String(real_temperatura) + ",";
  json += "\"hum\":" + String(real_humedad) + ",";
  json += "\"luz\":" + String(real_luz) + ",";
  json += "\"pres\":" + String(real_presion) + ",";
  json += "\"nivel_alerta\":" + String(nivel_alerta) + ",";
  json += "\"alerta\":" + String(hay_peligro ? "true" : "false") + ",";
  json += "\"silenciado\":" + String(buzzer_silenciado ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}
 
void manejarHistorico() {
  if (!server.authenticate(admin_user, admin_pass)) return server.requestAuthentication();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "[");
  for (int i = 0; i < cantidad_historico; i++) {
    int idx = (indice_historico - 1 - i + MAX_HISTORICO) % MAX_HISTORICO;
    String jsonChunk = "{";
    jsonChunk += "\"time\":\"" + historico[idx].timestamp + "\",";
    jsonChunk += "\"nivel\":" + String(historico[idx].nivel, 1) + ",";
    jsonChunk += "\"nivelPorc\":" + String(historico[idx].nivelPorc, 0) + ",";
    jsonChunk += "\"temp\":" + String(historico[idx].temp, 1) + ",";
    jsonChunk += "\"hum\":" + String(historico[idx].hum, 1) + ",";
    jsonChunk += "\"luz\":" + String(historico[idx].luz, 0) + ",";
    jsonChunk += "\"pres\":" + String(historico[idx].pres, 1) + ",";
    jsonChunk += "\"alerta\":" + String(historico[idx].alerta);
    jsonChunk += "}";
    if (i < cantidad_historico - 1) jsonChunk += ",";
    server.sendContent(jsonChunk);
  }
  server.sendContent("]");
  server.sendContent("");
}
 
void manejarSilenciar() {
  if (!server.authenticate(admin_user, admin_pass)) return server.requestAuthentication();
  buzzer_silenciado = true;
  digitalWrite(buzzerPin, LOW);
  server.send(200, "text/plain", "Silenciado");
}
 
// =========================================================================
// SETUP Y LOOP
// =========================================================================
void setup() {
  Serial.begin(115200);
 
  pinMode(buzzerPin, OUTPUT); digitalWrite(buzzerPin, LOW);
  pinMode(trigPin, OUTPUT); pinMode(echoPin, INPUT); digitalWrite(trigPin, LOW);
 
  pinMode(ledAzul, OUTPUT); pinMode(ledAmarillo, OUTPUT); pinMode(ledRojo, OUTPUT);
  digitalWrite(ledAzul, LOW); digitalWrite(ledAmarillo, LOW); digitalWrite(ledRojo, LOW);
 
  Wire.begin(21, 22);
  lcd.init(); lcd.backlight(); lcd.setCursor(0, 0); lcd.print("Iniciando...");
 
  sensorLuz.begin();
  bme.begin(0x76);
 
  Serial.print("Conectando a Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
 
  Serial.println("\nIP Web: http://" + WiFi.localIP().toString());
  lcd.clear(); lcd.print("WiFi OK");
 
  configTime(-18000, 0, "pool.ntp.org", "time.nist.gov");
 
  server.on("/", manejarRaiz);
  server.on("/datos", manejarDatos);
  server.on("/historico", manejarHistorico);
  server.on("/silenciar", HTTP_POST, manejarSilenciar);
  server.begin();
 
  xTaskCreate(TareaSensoresYAlarma, "Sensores", 8192, NULL, 1, NULL);
}
 
void loop() {
  server.handleClient();
  delay(2);
}
