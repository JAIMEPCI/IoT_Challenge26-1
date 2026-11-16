"""
ia_module.py - Módulo de Inteligencia Artificial del gateway.

Dos capas, para que el sistema nunca se quede sin respuesta:
  1. Predicción numérica local (determinista, auditable):
       - regresión lineal del nivel en la última hora -> pendiente %/h
       - horas de autonomía hasta llegar al nivel crítico (15 %)
       - detección de anomalías de consumo con puntaje z del caudal
  2. Recomendación con IA generativa (Gemini, API REST):
       recibe SOLO un resumen estadístico (no los datos crudos) y devuelve
       un JSON con riesgo, resumen y acciones para las autoridades.
       Si no hay API key o la llamada falla, se usa una recomendación por reglas.

Justificación para la wiki: el LLM no decide la alarma (eso lo hace la
fusión determinista); el LLM traduce números en recomendaciones de
racionamiento y comunicación para personas no técnicas.
"""
from __future__ import annotations

import json
import logging
import math
import sqlite3
import statistics
import time

import requests

log = logging.getLogger("ia")

GEMINI_URL = "https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent"
RIESGO_A_NUM = {"bajo": 0, "medio": 1, "alto": 2, "critico": 3}


# ---------------------------------------------------------------------------
# 1. Predicción local
# ---------------------------------------------------------------------------
def regresion_lineal(xs: list[float], ys: list[float]) -> tuple[float, float]:
    """Mínimos cuadrados: devuelve (pendiente, intercepto)."""
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return 0.0, my
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    m = sxy / sxx
    return m, my - m * mx


def resumen_estadistico(db: sqlite3.Connection, node: str, nivel_critico: float = 15.0) -> dict | None:
    ahora = time.time()
    filas = db.execute(
        "SELECT recibido, nivel_pct, caudal_lpm, temp_c, hum_pct, pres_hpa, rad_wm2, evap_mmh, alerta_gw "
        "FROM lecturas WHERE node_id=? AND recibido>=? ORDER BY recibido",
        (node, ahora - 6 * 3600),
    ).fetchall()
    if len(filas) < 5:
        return None

    ultima = filas[-1]
    hora = [f for f in filas if f[0] >= ahora - 3600 and f[1] is not None]
    pendiente = 0.0
    if len(hora) >= 5:
        xs = [(f[0] - hora[0][0]) / 3600 for f in hora]
        pendiente, _ = regresion_lineal(xs, [f[1] for f in hora])

    autonomia = None
    if ultima[1] is not None and pendiente < -0.1:
        autonomia = max(0.0, (ultima[1] - nivel_critico) / -pendiente)

    caudales = [f[2] for f in filas if f[2] is not None]
    z = 0.0
    if len(caudales) > 10 and statistics.pstdev(caudales) > 0:
        z = (caudales[-1] - statistics.mean(caudales)) / statistics.pstdev(caudales)

    def prom(i):
        v = [f[i] for f in hora if f[i] is not None]
        return round(statistics.mean(v), 2) if v else None

    return {
        "nodo": node,
        "nivel_actual_pct": ultima[1],
        "pendiente_nivel_pct_h": round(pendiente, 2),
        "horas_autonomia_hasta_15pct": None if autonomia is None else round(autonomia, 1),
        "caudal_actual_lpm": ultima[2],
        "caudal_promedio_6h_lpm": round(statistics.mean(caudales), 2) if caudales else None,
        "zscore_caudal": round(z, 2),
        "anomalia_consumo": abs(z) > 3,
        "temp_prom_1h_c": prom(3),
        "hum_prom_1h_pct": prom(4),
        "presion_prom_1h_hpa": prom(5),
        "radiacion_prom_1h_wm2": prom(6),
        "evaporacion_prom_1h_mmh": prom(7),
        "alerta_actual": ultima[8],
        "alertas_altas_6h": sum(1 for f in filas if (f[8] or 0) >= 2),
        "muestras_6h": len(filas),
    }


# ---------------------------------------------------------------------------
# 2. Recomendación (Gemini o reglas)
# ---------------------------------------------------------------------------
PROMPT = """Eres un asesor de gestión del riesgo hídrico para municipios de Sabana Centro
(Cundinamarca, Colombia) durante el Fenómeno de El Niño 2026. Recibes el resumen
estadístico de un tanque de almacenamiento comunitario monitoreado con IoT.
Las unidades están en el nombre de cada campo. El nivel crítico es 15 %.

Datos:
{datos}

Responde SOLO con un JSON con estas claves:
"riesgo": uno de "bajo", "medio", "alto", "critico";
"resumen": máximo 2 frases para la autoridad local;
"recomendaciones": lista de 2 a 4 acciones concretas (racionamiento, inspección
de fugas, comunicación a la comunidad, horarios de bombeo);
"mensaje_corto": máximo 120 caracteres para mostrar en la pantalla del nodo.
No inventes datos que no estén en el resumen."""


def recomendacion_reglas(r: dict) -> dict:
    """Respaldo determinista cuando no hay IA generativa disponible."""
    aut = r.get("horas_autonomia_hasta_15pct")
    nivel = r.get("nivel_actual_pct") or 0
    recs = []
    if aut is not None and aut < 6:
        riesgo = "critico"
        recs.append("Activar racionamiento por sectores de inmediato")
    elif (aut is not None and aut < 24) or nivel < 30:
        riesgo = "alto"
        recs.append("Programar racionamiento nocturno y avisar a la comunidad")
    elif nivel < 50 or (r.get("evaporacion_prom_1h_mmh") or 0) > 0.5:
        riesgo = "medio"
        recs.append("Reducir riego y usos no esenciales en horas de sol")
    else:
        riesgo = "bajo"
        recs.append("Mantener monitoreo y revisar el tanque semanalmente")
    if r.get("anomalia_consumo"):
        recs.append("Inspeccionar posibles fugas o extracciones no autorizadas")
    txt = f"Riesgo {riesgo}. " + (f"Autonomia aprox. {aut} h. " if aut is not None else "") + recs[0]
    return {"riesgo": riesgo, "resumen": txt, "recomendaciones": recs,
            "mensaje_corto": txt[:120], "fuente": "reglas"}


def consultar_gemini(r: dict, api_key: str, model: str, timeout: int = 30) -> dict:
    body = {
        "contents": [{"parts": [{"text": PROMPT.format(datos=json.dumps(r, ensure_ascii=False, indent=1))}]}],
        "generationConfig": {"responseMimeType": "application/json", "temperature": 0.3},
    }
    resp = requests.post(GEMINI_URL.format(model=model), json=body, timeout=timeout,
                         headers={"x-goog-api-key": api_key, "Content-Type": "application/json"})
    resp.raise_for_status()
    texto = resp.json()["candidates"][0]["content"]["parts"][0]["text"]
    out = json.loads(texto)
    if out.get("riesgo") not in RIESGO_A_NUM:
        raise ValueError(f"riesgo inválido: {out.get('riesgo')}")
    out["mensaje_corto"] = str(out.get("mensaje_corto", ""))[:120]
    out["fuente"] = "gemini"
    return out


def generar(db: sqlite3.Connection, node: str, api_key: str | None, model: str,
            lock=None, timeout: int = 90) -> dict | None:
    """Predicción + recomendación. La base de datos solo se bloquea para leer y
    escribir: mientras se espera a Gemini, el gateway sigue procesando datos."""
    from contextlib import nullcontext
    lock = lock or nullcontext()
    with lock:
        r = resumen_estadistico(db, node)
    if r is None:
        log.info("IA: pocos datos aún")
        return None
    try:
        if not api_key:
            raise RuntimeError("sin GEMINI_API_KEY")
        rec = consultar_gemini(r, api_key, model, timeout=timeout)
    except Exception as e:  # noqa: BLE001 - cualquier falla cae a reglas
        log.warning("IA generativa no disponible (%s); uso reglas", e)
        rec = recomendacion_reglas(r)
    rec["prediccion"] = r
    with lock:
        db.execute(
            "INSERT INTO ia(node_id, ts, horas_autonomia, riesgo, texto, json) VALUES (?,?,?,?,?,?)",
            (node, time.time(), r["horas_autonomia_hasta_15pct"], rec["riesgo"],
             rec["mensaje_corto"], json.dumps(rec, ensure_ascii=False)),
        )
        db.commit()
    return rec
