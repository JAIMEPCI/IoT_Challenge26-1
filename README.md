#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <BH1750.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <RTClib.h>

// =========================================================================
// CONFIGURACIÓN DE RED Y CREDENCIALES
// =========================================================================
const char* ssid = "PCI JAIME";        
const char* password = "S3cur3P@ssw0rd"; 
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

// Objetos I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);
BH1750 sensorLuz;
Adafruit_BME280 bme;
RTC_DS3231 rtc;

WebServer server(80);

// =========================================================================
// VARIABLES GLOBALES Y UMBRALES (FUSIÓN DE DATOS)
// =========================================================================
float real_distancia = 0.0;    
float real_temperatura = 0.0;  
float real_humedad = 0.0;      
float real_presion = 0.0;      
float real_luz = 0.0;          
float sim_caudal = 120.5;      
float sim_evap = 2.5;          

bool buzzer_silenciado = false; 
bool hay_peligro = false;       
int nivel_alerta = 0; // 0=Normal, 1=Media, 2=Crítica
float nivel_agua_porc = 0.0; // Porcentaje de llenado del tanque

// Parámetros para la Lógica de Fusión
const float ALTURA_TANQUE_CM = 100.0;  // <-- Ajusta esto a la altura real de tu tanque/recipiente
const float NIVEL_CRITICO_PORC = 20.0; // 20%
const float NIVEL_MEDIO_PORC = 40.0;   // 40%
const float TEMP_ALTA_C = 27.0;        // 28 °C
const float LUZ_ALTA_LUX = 40000.0;     // <-- Ajusta según la luz de tu entorno de prueba
const float HUMEDAD_BAJA_PORC = 55.0;  // 65%

// =========================================================================
// ESTRUCTURA DEL HISTÓRICO (Memoria de los últimos 60 datos)
// =========================================================================
struct RegistroDatos {
  char timestamp[20];
  float nivel;
  float temp;
  float hum;
  float luz;
  float pres;
};

const int MAX_HISTORICO = 300;
RegistroDatos historico[MAX_HISTORICO];
int indice_historico = 0;
int cantidad_historico = 0;

unsigned long ultimo_guardado = 0;
const unsigned long INTERVALO_GUARDADO = 10000; 

// =========================================================================
// FUNCIONES AUXILIARES
// =========================================================================
String obtenerFechaHora() {
  DateTime now = rtc.now(); 
  char buffer[25];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", 
           now.year(), now.month(), now.day(), 
           now.hour(), now.minute(), now.second());
  return String(buffer);
}

// -------------------------------------------------------------------------
// NUEVA FUNCIÓN: LÓGICA DE FUSIÓN DE DATOS
// -------------------------------------------------------------------------
void calcularLogicaFusion() {
  // 1. Convertir distancia cruda a % de nivel de agua
  nivel_agua_porc = ((ALTURA_TANQUE_CM - real_distancia) / ALTURA_TANQUE_CM) * 100.0;
  nivel_agua_porc = constrain(nivel_agua_porc, 0.0, 100.0); // Evita que pase de 100% o baje de 0%

  // 2. Evaluar condiciones individuales
  bool nivelCritico = nivel_agua_porc <= NIVEL_CRITICO_PORC;
  bool nivelMedio   = nivel_agua_porc <= NIVEL_MEDIO_PORC;
  bool tempAlta     = real_temperatura >= TEMP_ALTA_C;
  bool luzAlta      = real_luz >= LUZ_ALTA_LUX;
  bool humedadBaja  = real_humedad <= HUMEDAD_BAJA_PORC;

  // Riesgo evaporativo: Temp Alta o Luz Alta, y además Humedad Baja
  bool riesgoEvaporativo = (tempAlta || luzAlta) && humedadBaja;

  // 3. FUSIÓN: combinar variables para dictar el estado del sistema
  if (nivelCritico && riesgoEvaporativo) {
    nivel_alerta = 2; // CRÍTICA: nivel bajo + condición ambiental agravante simultánea
  } else if (nivelMedio || nivelCritico || riesgoEvaporativo) {
    nivel_alerta = 1; // MEDIA: nivel bajo/medio sin agravante, o riesgo evaporativo temprano
  } else {
    nivel_alerta = 0; // NORMAL
  }

  hay_peligro = (nivel_alerta == 2);
  
  if (nivel_alerta != 2) {
    buzzer_silenciado = false; // Se reinicia el silenciado al salir de estado crítico
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

    // 3. EJECUTAR LÓGICA DE FUSIÓN DE DATOS
    calcularLogicaFusion();

    // 4. CONTROL EXCLUSIVO DE LEDs SEGÚN ALERTA
    if (nivel_alerta == 0) {
      digitalWrite(ledAzul, HIGH); digitalWrite(ledAmarillo, LOW); digitalWrite(ledRojo, LOW);
    } else if (nivel_alerta == 1) {
      digitalWrite(ledAzul, LOW); digitalWrite(ledAmarillo, HIGH); digitalWrite(ledRojo, LOW);
    } else if (nivel_alerta == 2) {
      digitalWrite(ledAzul, LOW); digitalWrite(ledAmarillo, LOW); digitalWrite(ledRojo, HIGH);
    }

    // 5. CONTROL DEL BUZZER
    if (hay_peligro && !buzzer_silenciado) digitalWrite(buzzerPin, HIGH);
    else digitalWrite(buzzerPin, LOW);

    // 6. GUARDAR EN EL HISTÓRICO
    if (millis() - ultimo_guardado >= INTERVALO_GUARDADO || ultimo_guardado == 0) {
      obtenerFechaHora().toCharArray(historico[indice_historico].timestamp, 20);
      historico[indice_historico].nivel = real_distancia;
      historico[indice_historico].temp = real_temperatura;
      historico[indice_historico].hum = real_humedad;
      historico[indice_historico].luz = real_luz;
      historico[indice_historico].pres = real_presion;
      
      indice_historico = (indice_historico + 1) % MAX_HISTORICO;
      if (cantidad_historico < MAX_HISTORICO) cantidad_historico++;
      
      ultimo_guardado = millis();
    }

    // 7. ACTUALIZAR PANTALLA LCD
    lcd.clear();
    if (hay_peligro) {
      lcd.setCursor(0, 0); lcd.print("! ALERTA CRITICA!");
      lcd.setCursor(0, 1); lcd.print("Nivel: " + String(nivel_agua_porc, 1) + " %");
    } else if (nivel_alerta == 1) {
      lcd.setCursor(0, 0); lcd.print("! ALERTA MEDIA !");
      lcd.setCursor(0, 1); lcd.print("Nivel: " + String(nivel_agua_porc, 1) + " %");
    } else {
      int pantalla = (ciclo_lcd / 3) % 3; 
      if (pantalla == 0) {
        lcd.setCursor(0, 0); lcd.print("Temp: " + String(real_temperatura, 1) + " C");
        lcd.setCursor(0, 1); lcd.print("Hum : " + String(real_humedad, 1) + " %");
      } else if (pantalla == 1) {
        lcd.setCursor(0, 0); lcd.print("Luz: " + String(real_luz, 0) + " lx");
        lcd.setCursor(0, 1); lcd.print("Pre: " + String(real_presion, 1) + " mmHg");
      } else {
        lcd.setCursor(0, 0); lcd.print("Nivel de agua /");
        lcd.setCursor(0, 1); lcd.print(String(nivel_agua_porc, 1) + "% (" + String(real_distancia, 1) + "cm)");
      }
    }
    
    ciclo_lcd++;
    vTaskDelay(1000 / portTICK_PERIOD_MS); 
  }
}

// =========================================================================
// PÁGINA WEB MEJORADA (HTML + CSS + JavaScript)
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
    .header { text-align: center; margin-bottom: 30px; }
    .header h1 { color: var(--primary); margin: 0; font-size: 2.5em; text-transform: uppercase; letter-spacing: 2px;}
    .header p { color: #777; font-size: 1.1em; margin-top: 5px; }
    .grid-container { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; max-width: 1200px; margin: 0 auto; }
    .card { background: white; padding: 25px 15px; border-radius: 12px; box-shadow: 0 6px 12px rgba(0,0,0,0.05); text-align: center; border-top: 5px solid var(--primary); transition: all 0.3s ease; }
    .etiqueta { font-size: 1em; color: #888; font-weight: bold; text-transform: uppercase; }
    .valor { font-size: 3em; font-weight: 900; margin: 10px 0; color: #444; }
    .unidad { font-size: 0.4em; color: #999; vertical-align: middle; }
    
    /* Clases Peligro (Crítica) */
    .peligro-bg { background-color: #ffe6e6 !important; }
    .peligro-card { border-top-color: var(--danger) !important; animation: parpadeo 1.5s infinite alternate; }
    .peligro-text { color: var(--danger) !important; }
    
    /* Clases Warning (Media) */
    .warning-bg { background-color: #fff8e6 !important; }
    .warning-card { border-top-color: var(--warning) !important; }
    .warning-text { color: var(--warning) !important; }
    
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
    <h1>AQUA SABANA</h1>
    <p>Red de Monitoreo - Sabana Centro</p>
  </div>
  
  <div class="grid-container">
    <div class="card" id="card_nivel">
      <div class="etiqueta">Distancia / Nivel</div>
      <div class="valor" id="val_nivel">--<span class="unidad">cm</span></div>
    </div>
    <div class="card" id="card_temp">
      <div class="etiqueta">Temperatura</div>
      <div class="valor" id="val_temp">--<span class="unidad">°C</span></div>
    </div>
    <div class="card" id="card_humedad">
      <div class="etiqueta">Humedad Relativa</div>
      <div class="valor" id="val_humedad">--<span class="unidad">%</span></div>
    </div>
    <div class="card" id="card_rad">
      <div class="etiqueta">Iluminación</div>
      <div class="valor" id="val_rad">--<span class="unidad">Lux</span></div>
    </div>
    <div class="card">
      <div class="etiqueta">Presión Atmosférica</div>
      <div class="valor" id="val_pres">--<span class="unidad">mmHg</span></div>
    </div>
    
     <div class="btn-container">
    <button id="btn_silenciar" onclick="silenciarAlarma()">⚠️ DESACTIVAR ALARMA ⚠️</button>
    </div>

    <div class="card tabla-container">
      <h2 class="tabla-titulo">Historial de Mediciones (Últimos 60 datos)</h2>
      <table>
        <thead>
          <tr>
            <th>Fecha y Hora</th>
            <th>Nivel (cm)</th>
            <th>Temp (°C)</th>
            <th>Humedad (%)</th>
            <th>Luz (Lux)</th>
            <th>Presión (mmHg)</th>
          </tr>
        </thead>
        <tbody id="cuerpo_tabla">
          <tr><td colspan="6">Cargando datos...</td></tr>
        </tbody>
      </table>
    </div>
  </div>

  <script>
    setInterval(function() {
      fetch('/datos')
        .then(response => response.json())
        .then(data => {
          document.getElementById('val_nivel').innerHTML = data.nivel.toFixed(1) + '<span class="unidad">cm</span>';
          document.getElementById('val_temp').innerHTML = data.temp.toFixed(1) + '<span class="unidad">°C</span>';
          document.getElementById('val_humedad').innerHTML = data.hum.toFixed(1) + '<span class="unidad">%</span>';
          document.getElementById('val_rad').innerHTML = data.luz.toFixed(0) + '<span class="unidad">Lux</span>';
          document.getElementById('val_pres').innerHTML = data.pres.toFixed(1) + '<span class="unidad">mmHg</span>';
          
          let bodyEl = document.getElementById('cuerpo');
          let cardNivel = document.getElementById('card_nivel');
          let valNivel = document.getElementById('val_nivel');
          let btnSilenciar = document.getElementById('btn_silenciar');

          // Limpiar clases previas
          bodyEl.className = '';
          cardNivel.className = 'card';
          valNivel.className = 'valor';

          if (data.nivel_alerta === 2) {
            // ALERTA CRITICA
            bodyEl.classList.add('peligro-bg');
            cardNivel.classList.add('peligro-card');
            valNivel.classList.add('peligro-text');
            
            if(!data.silenciado) {
               btnSilenciar.style.display = 'inline-block';
               btnSilenciar.innerText = '⚠️ DESACTIVAR ALARMA ⚠️';
               btnSilenciar.classList.remove('silenciado');
            }
          } else if (data.nivel_alerta === 1) {
            // ALERTA MEDIA
            bodyEl.classList.add('warning-bg');
            cardNivel.classList.add('warning-card');
            valNivel.classList.add('warning-text');
            btnSilenciar.style.display = 'none'; // El buzzer no suena en alerta media
          } else {
            // NORMAL
            btnSilenciar.style.display = 'none';
          }
        });
    }, 1000);

    function actualizarHistorico() {
      fetch('/historico')
        .then(response => response.json())
        .then(data => {
          let tbody = document.getElementById('cuerpo_tabla');
          tbody.innerHTML = '';
          if(data.length === 0) {
             tbody.innerHTML = '<tr><td colspan="6">Aún no hay datos guardados.</td></tr>';
             return;
          }
          data.forEach(row => {
            tbody.innerHTML += `<tr>
              <td><strong>${row.time}</strong></td>
              <td>${row.nivel}</td>
              <td>${row.temp}</td>
              <td>${row.hum}</td>
              <td>${row.luz}</td>
              <td>${row.pres}</td>
            </tr>`;
          });
        });
    }
    setInterval(actualizarHistorico, 10000);
    actualizarHistorico();

    function silenciarAlarma() {
      fetch('/silenciar', { method: 'POST' })
        .then(response => {
          if(response.ok) {
            let btn = document.getElementById('btn_silenciar');
            btn.innerText = "✓ ALARMA SILENCIADA";
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
  json += "\"temp\":" + String(real_temperatura) + ",";
  json += "\"hum\":" + String(real_humedad) + ",";
  json += "\"luz\":" + String(real_luz) + ",";
  json += "\"pres\":" + String(real_presion) + ",";
  json += "\"nivel_alerta\":" + String(nivel_alerta) + ",";
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
    jsonChunk += "\"time\":\"";
    jsonChunk += historico[idx].timestamp;
    jsonChunk += "\",";
    jsonChunk += "\"nivel\":" + String(historico[idx].nivel, 1) + ",";
    jsonChunk += "\"temp\":" + String(historico[idx].temp, 1) + ",";
    jsonChunk += "\"hum\":" + String(historico[idx].hum, 1) + ",";
    jsonChunk += "\"luz\":" + String(historico[idx].luz, 0) + ",";
    jsonChunk += "\"pres\":" + String(historico[idx].pres, 1);
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
  
  if (!rtc.begin()) {
    Serial.println("No se encontro el RTC");
    lcd.setCursor(0, 1); lcd.print("Error RTC!      ");
    while (1); 
  }
  if (rtc.lostPower()) {
    Serial.println("RTC perdio energia. Fijando hora de compilacion...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  Serial.print("Conectando a Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  Serial.println("\nIP Web: http://" + WiFi.localIP().toString());
  lcd.clear(); lcd.print("WiFi OK");
  
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
