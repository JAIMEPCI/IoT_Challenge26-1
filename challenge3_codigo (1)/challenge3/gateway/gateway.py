"""
gateway.py - Gateway IoT en la Raspberry Pi.

Flujo:
  ESP32 --MQTT--> Mosquitto local --> este programa
      1. valida la lectura (rangos físicos)
      2. recalcula la fusión de alertas (fusion.py)
      3. guarda en SQLite (tabla lecturas, alertas, comandos, ia)
      4. reenvía a Ubidots por MQTT sobre TLS (store-and-forward: lo que no
         se pudo enviar queda con sincronizado=0 y se reintenta)
      5. cada IA_PERIODO_S ejecuta el módulo de IA y publica la recomendación
  Ubidots (botón "silenciar") --MQTT--> este programa --MQTT--> ESP32

Ejecutar:  python3 gateway.py            (lee config.env en la misma carpeta)
"""
from __future__ import annotations

import json
import logging
import os
import sqlite3
import ssl
import threading
import time
from pathlib import Path

import paho.mqtt.client as mqtt

import fusion
import ia_module

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(levelname)s %(message)s")
log = logging.getLogger("gateway")
BASE = Path(__file__).resolve().parent


# ---------------------------------------------------------------------------
# Configuración (archivo config.env: CLAVE=valor)
# ---------------------------------------------------------------------------
def cargar_config(ruta: Path) -> dict:
    cfg = {}
    if ruta.exists():
        for linea in ruta.read_text(encoding="utf-8").splitlines():
            linea = linea.strip()
            if linea and not linea.startswith("#") and "=" in linea:
                k, v = linea.split("=", 1)
                cfg[k.strip()] = v.strip()
    prefijos = ("MQTT_", "UBIDOTS_", "GEMINI_", "DB_", "IA_")
    cfg.update({k: v for k, v in os.environ.items() if k.startswith(prefijos)})
    return cfg


CFG = cargar_config(BASE / "config.env")
LOCAL_HOST = CFG.get("MQTT_LOCAL_HOST", "localhost")
LOCAL_PORT = int(CFG.get("MQTT_LOCAL_PORT", "1883"))
LOCAL_USER = CFG.get("MQTT_LOCAL_USER", "gateway")
LOCAL_PASS = CFG.get("MQTT_LOCAL_PASS", "")
UBI_HOST = CFG.get("UBIDOTS_HOST", "industrial.api.ubidots.com")
UBI_PORT = int(CFG.get("UBIDOTS_PORT", "8883"))
UBI_TOKEN = CFG.get("UBIDOTS_TOKEN", "")
DB_PATH = CFG.get("DB_PATH", str(BASE / "agua.db"))
GEMINI_KEY = CFG.get("GEMINI_API_KEY", "")
GEMINI_MODEL = CFG.get("GEMINI_MODEL", "gemini-flash-latest")
GEMINI_TIMEOUT_S = int(CFG.get("GEMINI_TIMEOUT_S", "90"))   # segundos esperando a Gemini
IA_PERIODO_S = int(CFG.get("IA_PERIODO_S", "600"))
# Cada cuánto se sube una lectura a la nube. Ubidots STEM (gratis) permite
# 4.000 datos/día: 8 variables cada 300 s = 2.304 datos/día, con margen para la
# IA y las alertas. Todo se guarda igual en SQLite; un cambio de alerta sube ya.
UBI_PERIODO_S = int(CFG.get("UBIDOTS_PERIODO_S", "300"))

# Variables que se envían a Ubidots. STEM permite 10 por dispositivo:
# estas 8 + riesgo_ia + silenciar = 10. Tasa, puntaje, causas y horas de
# autonomía viajan como "contexto" (texto extra) de alerta_gw y riesgo_ia.
VARIABLES = ["nivel_pct", "caudal_lpm", "temp_c", "hum_pct", "pres_hpa", "rad_wm2",
             "evap_mmh", "alerta_gw"]
UBI_PAUSA_S = 1.1   # STEM: máximo 1 envío por segundo

# Rangos físicos válidos; fuera de ellos el dato se descarta (None)
RANGOS = {"nivel_pct": (0, 100), "caudal_lpm": (0, 60), "temp_c": (-10, 60), "hum_pct": (0, 100),
          "pres_hpa": (500, 1100), "rad_wm2": (0, 1500), "evap_mmh": (0, 5)}


# ---------------------------------------------------------------------------
# Base de datos
# ---------------------------------------------------------------------------
ESQUEMA = """
CREATE TABLE IF NOT EXISTS lecturas(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  node_id TEXT NOT NULL, recibido REAL NOT NULL,
  nivel_pct REAL, dist_cm REAL, tasa_pct_h REAL, caudal_lpm REAL, volumen_l REAL,
  temp_c REAL, hum_pct REAL, pres_hpa REAL, rad_wm2 REAL, evap_mmh REAL,
  alerta_nodo INTEGER, alerta_gw INTEGER, puntaje INTEGER, causas TEXT,
  silenciada INTEGER, sincronizado INTEGER DEFAULT 0, ts_nodo REAL);
CREATE INDEX IF NOT EXISTS ix_lect ON lecturas(node_id, recibido);
CREATE TABLE IF NOT EXISTS alertas(
  id INTEGER PRIMARY KEY AUTOINCREMENT, node_id TEXT, ts REAL,
  nivel INTEGER, nombre TEXT, causas TEXT, silenciada INTEGER, origen TEXT);
CREATE TABLE IF NOT EXISTS comandos(
  id INTEGER PRIMARY KEY AUTOINCREMENT, node_id TEXT, ts REAL, origen TEXT, comando TEXT);
CREATE TABLE IF NOT EXISTS ia(
  id INTEGER PRIMARY KEY AUTOINCREMENT, node_id TEXT, ts REAL,
  horas_autonomia REAL, riesgo TEXT, texto TEXT, json TEXT);
CREATE TABLE IF NOT EXISTS estado_nodo(node_id TEXT PRIMARY KEY, estado TEXT, ts REAL);
"""

db_lock = threading.Lock()
db = sqlite3.connect(DB_PATH, check_same_thread=False)
db.executescript(ESQUEMA)
# Bases creadas con una versión anterior: agregar la columna de la hora del RTC
if "ts_nodo" not in [c[1] for c in db.execute("PRAGMA table_info(lecturas)")]:
    db.execute("ALTER TABLE lecturas ADD COLUMN ts_nodo REAL")
    db.commit()
filtros: dict[str, fusion.FiltroTemporal] = {}
ultimo_envio: dict[str, tuple[float, int]] = {}   # nodo -> (ts, alerta enviada)


def validar(d: dict) -> dict:
    out = dict(d)
    for k, (lo, hi) in RANGOS.items():
        v = out.get(k)
        if v is not None and not (lo <= v <= hi):
            log.warning("dato fuera de rango %s=%s -> descartado", k, v)
            out[k] = None
    return out


# ---------------------------------------------------------------------------
# Cliente Ubidots (nube)
# ---------------------------------------------------------------------------
ubi = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"gw-agua-{os.getpid()}")
ubi.username_pw_set(UBI_TOKEN, "")
if UBI_PORT == 8883:
    ubi.tls_set(cert_reqs=ssl.CERT_REQUIRED)
nodos_conocidos: set[str] = set()


def preparar_nodo_en_ubidots(c, nodo: str) -> None:
    """Crea la variable 'silenciar' (en 0) y se suscribe a ella."""
    c.publish(f"/v1.6/devices/{nodo}", json.dumps({"silenciar": 0}), qos=1)
    c.subscribe(f"/v1.6/devices/{nodo}/silenciar/lv", qos=1)


def ubi_on_connect(c, userdata, flags, reason_code, properties):
    log.info("Ubidots conectado: %s", reason_code)
    if reason_code.is_failure:
        log.error("Ubidots rechazó la conexión: revisa UBIDOTS_TOKEN")
    for n in nodos_conocidos:
        preparar_nodo_en_ubidots(c, n)


def ubi_on_message(c, userdata, msg):
    # /v1.6/devices/<nodo>/silenciar/lv  -> "1.0"
    partes = msg.topic.split("/")
    nodo = partes[3]
    try:
        valor = float(msg.payload.decode())
    except ValueError:
        return
    if valor >= 1:
        log.info("Ubidots pide silenciar %s", nodo)
        local.publish(f"sabana/{nodo}/cmd", json.dumps({"silenciar": True}), qos=1)
        with db_lock:
            db.execute("INSERT INTO comandos(node_id, ts, origen, comando) VALUES (?,?,?,?)",
                       (nodo, time.time(), "ubidots", "silenciar"))
            db.commit()
        # devolver el botón a 0 para el próximo uso
        c.publish(f"/v1.6/devices/{nodo}", json.dumps({"silenciar": 0}), qos=1)


ubi.on_connect = ubi_on_connect
ubi.on_message = ubi_on_message


sync_lock = threading.Lock()


def sincronizar_pendientes(max_filas: int = 20) -> None:
    """Store-and-forward: envía a Ubidots las lecturas aún no sincronizadas."""
    if not ubi.is_connected() or not sync_lock.acquire(blocking=False):
        return
    try:
        _sincronizar(max_filas)
    finally:
        sync_lock.release()


def _sincronizar(max_filas: int) -> None:
    with db_lock:
        filas = db.execute(
            f"SELECT id, node_id, recibido, causas, tasa_pct_h, puntaje, {', '.join(VARIABLES)} FROM lecturas "
            "WHERE sincronizado=0 ORDER BY id LIMIT ?", (max_filas,)).fetchall()
    for fila in filas:
        rid, nodo, ts, causas, tasa, puntaje, *valores = fila
        ts_ms = int(ts * 1000)
        carga = {}
        for k, v in zip(VARIABLES, valores):
            if v is None:
                continue
            item = {"value": v, "timestamp": ts_ms}
            if k == "alerta_gw":
                item["context"] = {"causas": (causas or "sin novedades")[:80], "puntaje": puntaje,
                                   "tasa": None if tasa is None else round(tasa, 1)}
            carga[k] = item
        info = ubi.publish(f"/v1.6/devices/{nodo}", json.dumps(carga), qos=1)
        try:
            info.wait_for_publish(timeout=5)
        except RuntimeError:
            return
        if info.is_published():
            with db_lock:
                db.execute("UPDATE lecturas SET sincronizado=1 WHERE id=?", (rid,))
                db.commit()
        else:
            return  # nube caída: se reintenta en el próximo ciclo
        time.sleep(UBI_PAUSA_S)


# ---------------------------------------------------------------------------
# Cliente MQTT local (Mosquitto)
# ---------------------------------------------------------------------------
local = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="gateway-agua")
local.username_pw_set(LOCAL_USER, LOCAL_PASS)


def local_on_connect(c, userdata, flags, reason_code, properties):
    log.info("Mosquitto local conectado: %s", reason_code)
    c.subscribe([("sabana/+/telemetria", 0), ("sabana/+/alerta", 1), ("sabana/+/estado", 1)])


def procesar_telemetria(nodo: str, d: dict) -> None:
    d = validar(d)
    res = fusion.evaluar(d)
    filtro = filtros.setdefault(nodo, fusion.FiltroTemporal())
    alerta_gw = filtro.actualizar(res.nivel)
    ahora = time.time()
    t_env, a_env = ultimo_envio.get(nodo, (0.0, -1))
    subir = (ahora - t_env >= UBI_PERIODO_S) or alerta_gw != a_env
    if subir:
        ultimo_envio[nodo] = (ahora, alerta_gw)
    # sincronizado: 0 = pendiente de subir, 1 = ya en Ubidots, 2 = solo local
    if d.get("alerta") is not None and d["alerta"] != alerta_gw:
        log.debug("nodo=%s alerta nodo=%s gw=%s", nodo, d["alerta"], alerta_gw)
    with db_lock:
        db.execute(
            "INSERT INTO lecturas(node_id, recibido, nivel_pct, dist_cm, tasa_pct_h, caudal_lpm, volumen_l,"
            " temp_c, hum_pct, pres_hpa, rad_wm2, evap_mmh, alerta_nodo, alerta_gw, puntaje, causas, silenciada,"
            " sincronizado, ts_nodo) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (nodo, ahora, d.get("nivel_pct"), d.get("dist_cm"), d.get("tasa_pct_h"),
             d.get("caudal_lpm"), d.get("volumen_l"), d.get("temp_c"), d.get("hum_pct"),
             d.get("pres_hpa"), d.get("rad_wm2"), d.get("evap_mmh"), d.get("alerta"),
             alerta_gw, res.puntaje, ", ".join(res.causas), int(bool(d.get("silenciada"))),
             0 if subir else 2, d.get("ts")))
        db.commit()
    if nodo not in nodos_conocidos:
        nodos_conocidos.add(nodo)
        if ubi.is_connected():
            preparar_nodo_en_ubidots(ubi, nodo)
    sincronizar_pendientes()


def local_on_message(c, userdata, msg):
    partes = msg.topic.split("/")
    if len(partes) != 3:
        return
    _, nodo, tipo = partes
    try:
        if tipo == "estado":
            with db_lock:
                db.execute("INSERT OR REPLACE INTO estado_nodo VALUES (?,?,?)",
                           (nodo, msg.payload.decode(), time.time()))
                db.commit()
            return
        d = json.loads(msg.payload)
    except (ValueError, UnicodeDecodeError):
        log.warning("mensaje inválido en %s", msg.topic)
        return
    if tipo == "telemetria":
        procesar_telemetria(nodo, d)
    elif tipo == "alerta":
        with db_lock:
            db.execute("INSERT INTO alertas(node_id, ts, nivel, nombre, causas, silenciada, origen)"
                       " VALUES (?,?,?,?,?,?,?)",
                       (nodo, time.time(), d.get("alerta"), d.get("nombre"), d.get("causas"),
                        int(bool(d.get("silenciada"))), "nodo"))
            db.commit()
        log.info("ALERTA %s: %s (%s)", nodo, d.get("nombre"), d.get("causas"))


local.on_connect = local_on_connect
local.on_message = local_on_message


# ---------------------------------------------------------------------------
# Hilos periódicos: sincronización e IA
# ---------------------------------------------------------------------------
def hilo_sincronizacion():
    while True:
        try:
            sincronizar_pendientes()
        except Exception as e:  # noqa: BLE001
            log.error("sincronización: %s", e)
        time.sleep(10)


def hilo_ia():
    time.sleep(int(CFG.get("IA_ESPERA_S", "60")))
    while True:
        for nodo in list(nodos_conocidos):
            try:
                rec = ia_module.generar(db, nodo, GEMINI_KEY, GEMINI_MODEL,
                                        lock=db_lock, timeout=GEMINI_TIMEOUT_S)
                if rec:
                    log.info("IA %s [%s]: %s", nodo, rec["fuente"], rec["mensaje_corto"])
                    local.publish(f"sabana/{nodo}/ia", json.dumps(
                        {"texto": rec["mensaje_corto"], "riesgo": rec["riesgo"]}, ensure_ascii=False),
                        qos=1, retain=True)
                    pred = rec["prediccion"]
                    carga = {"riesgo_ia": {"value": ia_module.RIESGO_A_NUM[rec["riesgo"]],
                                           "context": {"mensaje": rec["mensaje_corto"][:100],
                                                       "horas": pred["horas_autonomia_hasta_15pct"]}}}
                    if ubi.is_connected():
                        ubi.publish(f"/v1.6/devices/{nodo}", json.dumps(carga), qos=1)
            except Exception as e:  # noqa: BLE001
                log.error("IA %s: %s", nodo, e)
        time.sleep(IA_PERIODO_S)


def main():
    if not UBI_TOKEN:
        log.warning("UBIDOTS_TOKEN vacío: solo se guardará localmente")
    else:
        ubi.connect_async(UBI_HOST, UBI_PORT, keepalive=60)
        ubi.loop_start()
    local.connect_async(LOCAL_HOST, LOCAL_PORT, keepalive=60)
    threading.Thread(target=hilo_sincronizacion, daemon=True).start()
    threading.Thread(target=hilo_ia, daemon=True).start()
    log.info("Gateway iniciado. BD: %s", DB_PATH)
    local.loop_forever(retry_first_connection=True)


if __name__ == "__main__":
    main()
