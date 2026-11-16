# 2. Solución propuesta

## 2.1 Restricciones de diseño
<!-- Copien y ajusten la tabla de restricciones de la guía (técnicas, económicas, regulatorias, espacio, escalabilidad, temporales, energéticas). -->

| Tipo | Restricción | Cómo se resolvió |
|---|---|---|
| Técnica | Medición y transmisión fuera del hilo principal | ISR + tareas FreeRTOS |
| ... | ... | ... |

## 2.2 Arquitectura propuesta

### Diagrama de bloques de hardware
```mermaid
flowchart LR
  subgraph Nodo["Nodo IoT Aqua-Sabana (ESP32 CH340)"]
    US[HC-SR04 nivel] -->|GPIO5 TRIG / GPIO18 ECHO con divisor| ESP[ESP32]
    BME[BME280 T/HR/P 0x76] -->|I2C| ESP
    BH[BH1750 luz 0x23] -->|I2C| ESP
    RTC[RTC DS3231 0x68] -->|I2C| ESP
    ESP -->|I2C 0x27| LCD[LCD 16x2]
    ESP -->|GPIO23| BZ[Buzzer activo]
    ESP -->|GPIO25/26/27| LED[LEDs azul, amarillo, rojo]
  end
  ESP -. WiFi 2.4 GHz .-> RPI[Raspberry Pi 5 Gateway]
  RPI -. Internet .-> UBI[Ubidots]
  RPI -. HTTPS .-> GEM[API Gemini]
```
> El kit no incluye caudalímetro: el sistema mide nivel (el enunciado pide "nivel y/o caudal"). El firmware queda preparado para agregar un YF-S201 por interrupción (`CAUDAL_MODO`).

### Diagrama de bloques de software
```mermaid
flowchart LR
  subgraph ESP32
    ISR2[ISR pulsador] --> TU[Tarea UI: LCD/LED/buzzer]
    TS[Tarea sensado + fusión]
    TS --> EST[(Estado compartido + mutex)]
    EST --> TU
    EST --> TM[Tarea MQTT]
    EST --> TW[Tarea web: servidor + Digest]
  end
  TM <-->|MQTT| MOS[Mosquitto]
  subgraph Raspberry Pi
    MOS <--> GW[gateway.py]
    GW --> DB[(SQLite)]
    GW --> FU[fusion.py]
    GW --> IA[ia_module.py]
  end
  GW <-->|MQTT TLS| UB[Ubidots]
  IA -->|HTTPS| GE[Gemini]
```

## 2.3 Desarrollo teórico modular

### Criterios de diseño
<!-- Por qué cada sensor (rango, precisión, costo, interfaz), por qué MQTT (ligero, pub/sub, QoS, LWT), por qué SQLite, por qué Ubidots, por qué Gemini. -->

### Modelos matemáticos
- Nivel: N% = (d_vacío − d) / (d_vacío − d_lleno) × 100 (mediana de 5 lecturas)
- Caudal: Q [L/min] = f [Hz] / 7,5
- Tasa de cambio: ventana de 10 min, %/h
- Evaporación potencial: Priestley-Taylor horaria (ver guía)

### Lógica de fusión
<!-- Tabla de puntaje y de niveles, persistencia 3/6 evaluaciones, silenciamiento. -->

## 2.4 Diagramas UML

### Casos de uso
```mermaid
flowchart LR
  A((Autoridad local)) --> U1[Consultar tablero local]
  A --> U2[Consultar tablero global]
  A --> U3[Silenciar alarma]
  A --> U4[Recibir notificación]
  A --> U5[Consultar recomendación IA]
  C((Comunidad)) --> U6[Escuchar/ver alarma in situ]
  S((Sistema IoT)) --> U7[Medir y fusionar señales]
```

### Diagrama de secuencia: de la medición a la alerta global
```mermaid
sequenceDiagram
  participant S as Sensores
  participant E as ESP32
  participant M as Mosquitto
  participant G as Gateway
  participant D as SQLite
  participant U as Ubidots
  participant I as Gemini
  S->>E: lecturas (cada 1 s) + hora del RTC
  E->>E: fusión + persistencia (cada 5 s)
  E->>E: LCD, LEDs, buzzer
  E->>M: telemetria (cada 5 s), alerta (al cambiar)
  M->>G: telemetria
  G->>G: validar + recalcular fusión
  G->>D: INSERT lecturas (sincronizado=0)
  G->>U: publicar variables (TLS)
  G->>D: UPDATE sincronizado=1
  loop cada 10 min
    G->>D: leer últimas 6 h
    G->>I: resumen estadístico
    I-->>G: JSON riesgo + recomendaciones
    G->>M: sabana/nodo01/ia
    G->>U: riesgo_ia, horas_autonomia
  end
  U->>G: silenciar = 1
  G->>M: sabana/nodo01/cmd {"silenciar":true}
  M->>E: cmd
  E->>E: apaga buzzer
```

### Diagrama de estados de la alerta
```mermaid
stateDiagram-v2
  [*] --> NORMAL
  NORMAL --> PRECAUCION: puntaje 2-3 durante 15 s
  PRECAUCION --> ALERTA: puntaje 4-5 durante 15 s
  ALERTA --> CRITICO: puntaje >= 6 o nivel < 15 %
  PRECAUCION --> NORMAL: 30 s por debajo
  ALERTA --> PRECAUCION: 30 s por debajo
  CRITICO --> ALERTA: 30 s por debajo
  ALERTA --> ALERTA_SILENCIADA: pulsador / web / Ubidots
  ALERTA_SILENCIADA --> ALERTA: 30 min o sube de nivel
```

### Diagrama de clases del gateway
```mermaid
classDiagram
  class Gateway {
    +procesar_telemetria(nodo, d)
    +sincronizar_pendientes()
    +hilo_ia()
  }
  class FiltroTemporal {
    +subir: int
    +bajar: int
    +actual: int
    +actualizar(candidato) int
  }
  class ResultadoFusion {
    +nivel: int
    +puntaje: int
    +causas: list
  }
  class IAModule {
    +resumen_estadistico(db, node) dict
    +consultar_gemini(r, key, model) dict
    +recomendacion_reglas(r) dict
  }
  Gateway --> FiltroTemporal
  Gateway ..> ResultadoFusion
  Gateway --> IAModule
```

### Modelo de datos (SQLite)
```mermaid
erDiagram
  LECTURAS {
    int id
    text node_id
    real recibido
    real nivel_pct
    real caudal_lpm
    real temp_c
    real hum_pct
    real pres_hpa
    real rad_wm2
    real evap_mmh
    int alerta_gw
    int sincronizado
  }
  ALERTAS {
    int id
    text node_id
    real ts
    int nivel
    text causas
  }
  COMANDOS {
    int id
    text node_id
    real ts
    text origen
    text comando
  }
  IA {
    int id
    text node_id
    real ts
    real horas_autonomia
    text riesgo
    text texto
  }
```

## 2.5 Esquemático de hardware
<!-- Imagen del esquemático (KiCad/Fritzing/Wokwi), tabla de pines, divisores de voltaje, transistor del buzzer. -->
![Esquemático](imagenes/esquematico.png)

## 2.6 Estándares de ingeniería aplicados
| Estándar | Aplicación |
|---|---|
| IEEE 802.11 b/g/n | WiFi del ESP32 y la Raspberry |
| MQTT 3.1.1 (OASIS, ISO/IEC 20922) | Comunicación nodo–gateway–nube, QoS, Last Will |
| TLS 1.2 | MQTT seguro hacia Ubidots (puerto 8883) |
| RFC 7616 (HTTP Digest) | Autenticación del tablero local |
| RFC 8259 (JSON) | Formato de mensajes |
| I2C (NXP UM10204) | BME280, BH1750, DS3231, LCD 16x2 |
| ISO/IEC 30141 | Arquitectura de referencia IoT (capas) |
| UML 2.5 (ISO/IEC 19505) | Diagramas de diseño |
| IEC 60529 (IP65) | Grado de protección de la caja |
| Ley 1581 de 2012 | Protección de datos de los usuarios |
| Resolución 0330 de 2017 (RAS) | Contexto de sistemas de agua potable |

## 2.7 Modelo de negocio
| Bloque (Lean Canvas) | Contenido |
|---|---|
| Problema | Municipios sin monitoreo en tiempo real de tanques comunitarios durante El Niño |
| Clientes | Alcaldías de Sabana Centro, acueductos veredales, juntas de acción comunal, distritos de riego |
| Propuesta de valor | Alerta temprana validada + predicción de autonomía por < 250.000 COP por punto |
| Solución | Nodo IoT + gateway local + tablero en la nube + IA |
| Canales | CAR, UAEGRD, convocatorias de gestión del riesgo |
| Ingresos | Venta del nodo + suscripción mensual por punto monitoreado (tablero, IA, soporte) |
| Costos | Hardware, nube, API de IA, instalación y mantenimiento |
| Métricas | Puntos monitoreados, horas de anticipación de alertas, falsas alarmas |
| Ventaja | Bajo costo y alarma local que funciona sin Internet |
