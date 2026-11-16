"""
simular_nodo.py - Publica telemetría falsa como si fuera el ESP32.

Sirve para que la persona del gateway trabaje sin esperar el hardware y para
probar escenarios de la tabla de pruebas (P-xx) de forma repetible.

Uso:
  python3 simular_nodo.py --escenario sequia --host localhost --user nodo01 --password clave
Escenarios: normal, sequia (nivel cae rápido con calor seco), fuga (nivel cae sin caudal)
"""
import argparse
import json
import random
import time

import paho.mqtt.client as mqtt

from fusion import evaporacion_pt

p = argparse.ArgumentParser()
p.add_argument("--host", default="localhost")
p.add_argument("--port", type=int, default=1883)
p.add_argument("--user", default="nodo01")
p.add_argument("--password", default="")
p.add_argument("--node", default="nodo01")
p.add_argument("--escenario", choices=["normal", "sequia", "fuga"], default="sequia")
p.add_argument("--periodo", type=float, default=5.0)
a = p.parse_args()

c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"sim-{a.node}")
c.username_pw_set(a.user, a.password)
c.will_set(f"sabana/{a.node}/estado", "offline", qos=1, retain=True)
c.connect(a.host, a.port)
c.loop_start()
c.publish(f"sabana/{a.node}/estado", "online", qos=1, retain=True)

nivel, volumen, hist = 85.0, 0.0, []
desc = {"normal": 0.0, "sequia": 0.08, "fuga": 0.05}[a.escenario]   # % por segundo
t0 = time.time()
while True:
    dt = a.periodo
    caudal = 0.0 if a.escenario == "fuga" else max(0, random.gauss(3 if desc else 1, 0.3))
    nivel = max(0.0, nivel - desc * dt + random.gauss(0, 0.2))
    volumen += caudal / 60 * dt
    hist.append((time.time(), nivel))
    hist = [h for h in hist if h[0] >= time.time() - 600]
    minutos = (hist[-1][0] - hist[0][0]) / 60
    tasa = (hist[-1][1] - hist[0][1]) / minutos * 60 if minutos >= 1 else 0.0
    calor = a.escenario == "sequia"
    temp = random.gauss(24 if calor else 15, 0.3)
    hum = random.gauss(33 if calor else 65, 1)
    rad = random.gauss(650 if calor else 250, 20)
    pres = random.gauss(752, 0.5)
    d = {"node": a.node, "uptime_s": int(time.time() - t0), "nivel_pct": round(nivel, 2),
         "tasa_pct_h": round(tasa, 2), "caudal_lpm": round(caudal, 2), "volumen_l": round(volumen, 2),
         "temp_c": round(temp, 2), "hum_pct": round(hum, 1), "pres_hpa": round(pres, 1),
         "rad_wm2": round(rad), "evap_mmh": round(evaporacion_pt(temp, pres, rad), 3),
         "alerta": 0, "puntaje": 0, "causas": 0, "silenciada": False}
    c.publish(f"sabana/{a.node}/telemetria", json.dumps(d))
    print(json.dumps(d))
    time.sleep(dt)
