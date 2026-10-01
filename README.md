# IoT_Challenge26-1

  SISTEMA IoT DE MONITOREO HÍDRICO - SABANA CENTRO
  Internet de las Cosas - Universidad de La Sabana - 2026-2
  Challenge #1 - VERSIÓN FINAL PARA HARDWARE FÍSICO

  Componentes:
  - ESP32 DevKit (chip USB-Serial CH340)
  - Sensor ultrasónico HC-SR04 (nivel de agua)
  - BME280 (temperatura, humedad, presión)
  - LDR + resistencia 10kΩ (aproximación de radiación solar)
  - Pantalla LCD 16x2 con módulo I2C
  - Buzzer (alarma sonora)
  - LEDs rojo, amarillo, verde (alarma visual)

  Autores: Jaime y Andrés Daza


#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// ============================================================
// DEFINICIÓN DE PINES (según cableado real en protoboard)
// ============================================================
#define PIN_TRIG      5
#define PIN_ECHO      18
#define PIN_LDR       34   // Pin ADC (analógico)
#define PIN_BUZZER    25
#define PIN_LED_VERDE   26
#define PIN_LED_AMARILLO 27
#define PIN_LED_ROJO    32

// ============================================================
// OBJETOS DE SENSORES Y PANTALLA
// ============================================================
Adafruit_BME280 bme;                // Sensor climático (I2C)
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Dirección I2C típica 0x27 (probar 0x3F si no responde)

bool bmeConectado = false; // Bandera para saber si el BME280 respondió bien

// ============================================================
// PARÁMETROS DE CALIBRACIÓN (AJUSTAR SEGÚN SU TANQUE/BALDE)
// ============================================================
const float ALTURA_TANQUE_CM = 30.0;   // Altura total del balde/recipiente usado
const int   NIVEL_CRITICO_PORC = 20;   // % de nivel considerado crítico
const int   NIVEL_MEDIO_PORC   = 40;   // % de nivel considerado alerta media
const float TEMP_ALTA_C        = 28.0; // Temperatura considerada "alta"
const int   LUZ_ALTA_UMBRAL    = 3000; // Valor ADC (0-4095) considerado luz alta

// ============================================================
// VARIABLES GLOBALES
// ============================================================
float nivelAguaCM = 0;
float nivelAguaPorc = 0;
float temperatura = 0;
float humedad = 0;
float presion = 0;
int   valorLuz = 0;

// Control de tiempo sin usar delay() bloqueante
unsigned long tiempoAnterior = 0;
const unsigned long INTERVALO_LECTURA = 2000; // Leer sensores cada 2 segundos

unsigned long tiempoBuzzerAnterior = 0;
bool buzzerEstado = false;
const unsigned long INTERVALO_BUZZER = 400; // Parpadeo/beep cada 400 ms

// Alternar pantallas en el LCD
unsigned long tiempoPantallaAnterior = 0;
const unsigned long INTERVALO_PANTALLA = 3000;
int pantallaActual = 0;

// Nivel de alerta: 0 = Normal, 1 = Media, 2 = Crítica
int nivelAlerta = 0;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500); // Pequeña espera para estabilizar el puerto serial en hardware real

  // Pines de salida
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMARILLO, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);

  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_AMARILLO, LOW);
  digitalWrite(PIN_LED_ROJO, LOW);

  // Iniciar I2C (SDA=21, SCL=22) con un poco de tiempo de estabilización
  Wire.begin(21, 22);
  delay(300);

  // Iniciar LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Sistema IoT");
  lcd.setCursor(0, 1);
  lcd.print("Monitoreo Agua");
  Serial.println("LCD iniciado correctamente.");

  // Iniciar BME280 (probar primero 0x76, si falla probar 0x77)
  if (bme.begin(0x76)) {
    bmeConectado = true;
    Serial.println("BME280 conectado correctamente (0x76).");
  } else if (bme.begin(0x77)) {
    bmeConectado = true;
    Serial.println("BME280 conectado correctamente (0x77).");
  } else {
    bmeConectado = false;
    Serial.println("ERROR: No se encontró el sensor BME280. Revisar cableado I2C.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Error BME280!");
    lcd.setCursor(0, 1);
    lcd.print("Revisar cables");
    delay(3000);
  }

  delay(1500);
  lcd.clear();

  Serial.println("=== Sistema iniciado correctamente ===");
}

// ============================================================
// LOOP PRINCIPAL
// ============================================================
void loop() {
  unsigned long tiempoActual = millis();

  // --- Leer sensores cada INTERVALO_LECTURA ---
  if (tiempoActual - tiempoAnterior >= INTERVALO_LECTURA) {
    tiempoAnterior = tiempoActual;

    leerNivelAgua();
    if (bmeConectado) {
      leerClima();
    }
    leerLuz();
    calcularAlerta();
    mostrarEnSerial();
  }

  // --- Actualizar pantalla LCD ---
  if (tiempoActual - tiempoPantallaAnterior >= INTERVALO_PANTALLA) {
    tiempoPantallaAnterior = tiempoActual;
    actualizarLCD();
  }

  // --- Controlar salidas (LEDs y buzzer) sin bloquear el loop ---
  controlarSalidas(tiempoActual);
}

// ============================================================
// FUNCIÓN: Leer nivel de agua con HC-SR04
// ============================================================
void leerNivelAgua() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duracion = pulseIn(PIN_ECHO, HIGH, 30000); // timeout 30ms

  if (duracion == 0) {
    Serial.println("Advertencia: sin lectura del HC-SR04 (revisar cableado Trig/Echo).");
    return; // Mantiene el último valor válido
  }

  float distanciaCM = duracion * 0.0343 / 2.0; // velocidad del sonido

  nivelAguaCM = ALTURA_TANQUE_CM - distanciaCM;
  if (nivelAguaCM < 0) nivelAguaCM = 0;
  if (nivelAguaCM > ALTURA_TANQUE_CM) nivelAguaCM = ALTURA_TANQUE_CM;

  nivelAguaPorc = (nivelAguaCM / ALTURA_TANQUE_CM) * 100.0;
}

// ============================================================
// FUNCIÓN: Leer temperatura, humedad y presión con BME280
// ============================================================
void leerClima() {
  temperatura = bme.readTemperature();
  humedad = bme.readHumidity();
  presion = bme.readPressure() / 100.0F; // Pa -> hPa
}

// ============================================================
// FUNCIÓN: Leer valor de luz con el LDR
// ============================================================
void leerLuz() {
  valorLuz = analogRead(PIN_LDR); // Rango 0-4095 en ESP32
}

// ============================================================
// FUNCIÓN: Lógica de fusión de señales para determinar alerta
// ============================================================
void calcularAlerta() {
  bool nivelCritico = nivelAguaPorc <= NIVEL_CRITICO_PORC;
  bool nivelMedio    = nivelAguaPorc <= NIVEL_MEDIO_PORC;
  bool tempAlta       = bmeConectado && (temperatura >= TEMP_ALTA_C);
  bool luzAlta         = valorLuz >= LUZ_ALTA_UMBRAL;

  // Alerta CRÍTICA: nivel crítico + al menos una condición ambiental agravante
  if (nivelCritico && (tempAlta || luzAlta)) {
    nivelAlerta = 2;
  }
  // Alerta MEDIA: nivel medio-bajo, o nivel crítico sin condiciones agravantes
  else if (nivelMedio || nivelCritico) {
    nivelAlerta = 1;
  }
  // Normal
  else {
    nivelAlerta = 0;
  }
}

// ============================================================
// FUNCIÓN: Controlar LEDs y buzzer según nivel de alerta
// ============================================================
void controlarSalidas(unsigned long tiempoActual) {
  switch (nivelAlerta) {

    case 0: // Normal
      digitalWrite(PIN_LED_VERDE, HIGH);
      digitalWrite(PIN_LED_AMARILLO, LOW);
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      break;

    case 1: // Media
      digitalWrite(PIN_LED_VERDE, LOW);
      digitalWrite(PIN_LED_AMARILLO, HIGH);
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_BUZZER, LOW); // Sin sonido en alerta media
      break;

    case 2: // Crítica
      digitalWrite(PIN_LED_VERDE, LOW);
      digitalWrite(PIN_LED_AMARILLO, LOW);
      digitalWrite(PIN_LED_ROJO, HIGH);

      // Buzzer intermitente (no bloqueante)
      if (tiempoActual - tiempoBuzzerAnterior >= INTERVALO_BUZZER) {
        tiempoBuzzerAnterior = tiempoActual;
        buzzerEstado = !buzzerEstado;
        digitalWrite(PIN_BUZZER, buzzerEstado);
      }
      break;
  }
}

// ============================================================
// FUNCIÓN: Mostrar datos en el LCD (alternando pantallas)
// ============================================================
void actualizarLCD() {
  lcd.clear();

  switch (pantallaActual) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("Nivel: ");
      lcd.print(nivelAguaPorc, 1);
      lcd.print(" %");
      lcd.setCursor(0, 1);
      lcd.print("Estado: ");
      lcd.print(textoAlerta());
      break;

    case 1:
      lcd.setCursor(0, 0);
      if (bmeConectado) {
        lcd.print("Temp: ");
        lcd.print(temperatura, 1);
        lcd.print("C");
      } else {
        lcd.print("BME280 error");
      }
      lcd.setCursor(0, 1);
      if (bmeConectado) {
        lcd.print("Hum: ");
        lcd.print(humedad, 1);
        lcd.print(" %");
      }
      break;

    case 2:
      lcd.setCursor(0, 0);
      if (bmeConectado) {
        lcd.print("Presion:");
        lcd.print(presion, 0);
        lcd.print("hPa");
      } else {
        lcd.print("Sin datos");
      }
      lcd.setCursor(0, 1);
      lcd.print("Luz: ");
      lcd.print(valorLuz);
      break;
  }

  pantallaActual = (pantallaActual + 1) % 3; // Ciclo entre 3 pantallas
}

// ============================================================
// FUNCIÓN: Texto descriptivo del nivel de alerta
// ============================================================
String textoAlerta() {
  if (nivelAlerta == 2) return "CRITICO";
  if (nivelAlerta == 1) return "Media";
  return "Normal";
}

// ============================================================
// FUNCIÓN: Mostrar todo por el monitor serial (para depuración)
// ============================================================
void mostrarEnSerial() {
  Serial.println("---------------------------------");
  Serial.print("Nivel agua: ");
  Serial.print(nivelAguaPorc);
  Serial.println(" %");

  if (bmeConectado) {
    Serial.print("Temperatura: ");
    Serial.print(temperatura);
    Serial.println(" C");

    Serial.print("Humedad: ");
    Serial.print(humedad);
    Serial.println(" %");

    Serial.print("Presion: ");
    Serial.print(presion);
    Serial.println(" hPa");
  } else {
    Serial.println("BME280 no conectado - sin datos de clima");
  }

  Serial.print("Luz (LDR): ");
  Serial.println(valorLuz);

  Serial.print("Nivel de alerta: ");
  Serial.println(textoAlerta());
}
