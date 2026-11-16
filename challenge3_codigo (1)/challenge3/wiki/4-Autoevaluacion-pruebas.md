# 4. Autoevaluación del protocolo de pruebas

Resultado: ✅ cumple · ⚠️ cumple parcialmente · ❌ no cumple. Cada fila enlaza a su evidencia en [Resultados](3-Resultados) o en el video.

| ID | Requisito que valida | Procedimiento | Criterio de aceptación | Resultado | Evidencia |
|---|---|---|---|---|---|
| P-01 | Medición de nivel | 5 alturas medidas con regla | Error ≤ 1 cm | | |
| P-02 | Medición de caudal | Llenar recipiente graduado de 1 L | Error de volumen ≤ 10 % | | |
| P-03 | T, HR, P | Comparar con referencia | T ±1 °C, HR ±5 %, P ≈ 750 hPa en Chía | | |
| P-04 | Radiación | Sombra vs sol directo | Cambio coherente, sin saturar | | |
| P-05 | ISR / hilos | Pulsos conocidos mientras se usa la web | Sin pulsos perdidos | | |
| P-06 | Fusión sin falsos positivos | Tanque lleno 10 min | Siempre NORMAL | | |
| P-07 | Fusión en sequía | Bomba encendida + calor | Escala NORMAL→PRECAUCIÓN→ALERTA→CRÍTICO | | |
| P-08 | Validación temporal | Obstáculo 5 s frente al sensor | No cambia la alerta | | |
| P-09 | Detección de anomalía | Sacar agua sin pasar por el caudalímetro | Causa "posible fuga" | | |
| P-10 | Desactivar alarma | Pulsador, web local y Ubidots | Buzzer apaga ≤ 2 s (local) / ≤ 10 s (nube) | | |
| P-11 | Acceso restringido local | Entrar sin y con credenciales, PC y celular | 401 sin clave; acceso con clave | | |
| P-12 | Seguridad MQTT | Cliente anónimo y publicación no permitida | Rechazados por Mosquitto | | |
| P-13 | Almacenamiento local | Cortar Internet de la Raspberry 5 min | 100 % de lecturas llegan luego a Ubidots | | |
| P-14 | Alerta sin red | Apagar la Raspberry | ESP32 sigue alarmando y sirviendo la web | | |
| P-15 | Tiempo real | 10 mediciones de latencia | Promedio ≤ 10 s | | |
| P-16 | Módulo de IA | Con y sin API key | Recomendación coherente; respaldo por reglas | | |
| P-17 | Tablero global | Abrir Ubidots con datos móviles | Acceso solo con usuario autorizado | | |
| P-18 | Notificación | Forzar ALERTA | Llega correo/Telegram/SMS de Ubidots | | |

## Reflexión
<!-- Qué pruebas fallaron o quedaron parciales, por qué y qué cambiarían. Sean honestos: la rúbrica valora el análisis crítico. -->
