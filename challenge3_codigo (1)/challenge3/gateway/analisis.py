"""
analisis.py - Exporta datos de SQLite a CSV y genera gráficas para la wiki
(sección "Configuración experimental, resultados y análisis").

Uso:  python3 analisis.py --db agua.db --node nodo01 --horas 2
Salida: resultados/lecturas.csv, resultados/*.png y métricas en consola.
"""
import argparse
import csv
import sqlite3
import time
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

p = argparse.ArgumentParser()
p.add_argument("--db", default="agua.db")
p.add_argument("--node", default="nodo01")
p.add_argument("--horas", type=float, default=2)
a = p.parse_args()

out = Path("resultados"); out.mkdir(exist_ok=True)
db = sqlite3.connect(a.db)
cols = ["recibido", "nivel_pct", "tasa_pct_h", "caudal_lpm", "temp_c", "hum_pct", "pres_hpa",
        "rad_wm2", "evap_mmh", "alerta_nodo", "alerta_gw", "puntaje", "causas", "sincronizado"]
filas = db.execute(f"SELECT {', '.join(cols)} FROM lecturas WHERE node_id=? AND recibido>=? ORDER BY recibido",
                   (a.node, time.time() - a.horas * 3600)).fetchall()
if not filas:
    raise SystemExit("No hay lecturas en ese intervalo")

with open(out / "lecturas.csv", "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f); w.writerow(cols); w.writerows(filas)

t0 = filas[0][0]
t = [(f[0] - t0) / 60 for f in filas]
col = {c: [f[i] for f in filas] for i, c in enumerate(cols)}

fig, ax = plt.subplots(3, 1, figsize=(9, 8), sharex=True)
ax[0].plot(t, col["nivel_pct"], label="Nivel %"); ax[0].set_ylabel("Nivel (%)")
ax0b = ax[0].twinx(); ax0b.step(t, col["alerta_gw"], color="red", where="post", label="Alerta")
ax0b.set_ylabel("Alerta (0-3)"); ax0b.set_ylim(-0.2, 3.2)
ax[1].plot(t, col["caudal_lpm"], color="green"); ax[1].set_ylabel("Caudal (L/min)")
ax[2].plot(t, col["temp_c"], label="T (°C)"); ax[2].plot(t, col["hum_pct"], label="HR (%)")
ax[2].set_ylabel("Clima"); ax[2].legend(); ax[2].set_xlabel("Tiempo (min)")
fig.suptitle(f"Prueba {a.node}: nivel, alerta, caudal y clima")
fig.tight_layout(); fig.savefig(out / "serie_tiempo.png", dpi=150)

# Métricas útiles para el análisis
n = len(filas)
sync = sum(1 for v in col["sincronizado"] if v == 1) / max(1, sum(1 for v in col["sincronizado"] if v != 2)) * 100
coinc = sum(1 for x, y in zip(col["alerta_nodo"], col["alerta_gw"]) if x == y) / n * 100
intervalos = [filas[i][0] - filas[i - 1][0] for i in range(1, n)]
print(f"Lecturas: {n}  |  periodo medio: {sum(intervalos)/len(intervalos):.2f} s" if intervalos else "")
print(f"Sincronizadas con Ubidots: {sync:.1f} %")
print(f"Coincidencia alerta nodo vs gateway: {coinc:.1f} %")
for k in ("nivel_pct", "temp_c", "hum_pct", "pres_hpa"):
    v = [x for x in col[k] if x is not None]
    if v:
        print(f"{k}: min {min(v):.2f}  max {max(v):.2f}  media {sum(v)/len(v):.2f}")
print("Archivos en", out.resolve())
