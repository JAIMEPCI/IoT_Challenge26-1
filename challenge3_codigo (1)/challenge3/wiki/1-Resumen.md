# 1. Resumen general, motivación y justificación

## Resumen
<!-- 1 párrafo: qué hicieron, con qué, qué resultado clave obtuvieron (con números de sus pruebas). -->
Se diseñó e implementó un prototipo IoT ... El nodo ESP32 publica cada 5 s por MQTT ... la alerta se valida con persistencia de 15 s ... la latencia medida nodo→Ubidots fue de __ s ... el módulo de IA estima __ horas de autonomía ...

## Motivación
- Confirmación del inicio anticipado de El Niño en 2026 (Minambiente) [1].
- 20 municipios de Cundinamarca en riesgo extremo de desabastecimiento según la CAR [2].
- Bogotá identifica 16 escenarios de riesgo, entre ellos el desabastecimiento de agua [3].
- Plan de contingencia departamental que pide reforzar el monitoreo hidrometeorológico [4].
<!-- Parafraseen con sus palabras; no copien párrafos de las fuentes. -->

## Justificación
<!-- Por qué IoT complementa la red institucional: costo, densidad de puntos, tiempo real, cruce de variables hidráulicas y atmosféricas, alerta local aunque no haya Internet. -->

## Pregunta guía
¿Cómo diseñar un sistema IoT que mida y analice en tiempo real el nivel y/o caudal del agua, la evaporación potencial y variables meteorológicas en Sabana Centro, combinando señales mediante lógica de fusión para generar alertas válidas in situ y en un tablero local y global?

## Objetivos
- General: ...
- Específicos: (1) medir ..., (2) fusionar ..., (3) notificar ..., (4) almacenar y transmitir por MQTT ..., (5) integrar IA ...

## Estructura del documento
Ver la página [Inicio](Home).
