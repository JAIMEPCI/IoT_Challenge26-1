# Declaración de uso de herramientas de Inteligencia Artificial

Esta sección es obligatoria según el enunciado: sin ella el trabajo no se evalúa si se evidencia uso de IA.

## 1. Herramientas consultadas
| Herramienta | Uso |
|---|---|
| Claude (Anthropic) | Planeación del reto, código base del firmware y del gateway, plantillas de esta wiki |
| Gemini API (Google) | Componente del sistema: módulo de recomendación (no se usó para redactar) |
| [otras] | |

## 2. Conversaciones y prompts
- Enlace a la conversación con Claude: [pegar enlace compartido de la conversación]
- Prompt inicial (transcrito exactamente): "Mira Tengo que hacer eso para una clase ayudame con la preparacion de todo y ayudame con el desarollo de esto dame el paso a paso completo del desarrollo al 100% de esto para realizar la actividad de esto somos un equipo de 2"
- [Agregar aquí cada pregunta posterior que le hagan a una IA]

## 3. Validación y aplicación
| Respuesta de la IA | Cómo la validamos | Cómo se aplicó / qué cambiamos |
|---|---|---|
| Fórmula de evaporación Priestley-Taylor | Contrastada con FAO-56 y cálculo manual | Implementada en `fusion.py` y en el ESP32 |
| Pinout y divisores de voltaje | Hojas de datos del ESP32 y sensores; medición con multímetro | |
| Código del firmware | Compilación, Wokwi y pruebas P-01 a P-18 | |
| Código del gateway | `python3 fusion.py`, simulador y pruebas | |
| Umbrales de alerta | Datos climáticos del IDEAM para la Sabana | Ajustados a __ |
