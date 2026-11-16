"""
fusion.py - Lógica de fusión de señales (misma regla que el firmware del ESP32).

El gateway la recalcula para:
  1. tener una segunda opinión independiente del nodo (detecta firmware o
     sensores defectuosos cuando ambas difieren), y
  2. guardar en SQLite el puntaje y las causas de cada lectura.
Los umbrales se documentan en la wiki (sección Lógica de fusión).
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field

UMBRALES = {
    "nivel_critico": 15.0,   # %
    "nivel_bajo": 30.0,      # %
    "nivel_medio": 50.0,     # %
    "desc_leve": -5.0,       # %/h
    "desc_fuerte": -10.0,    # %/h
    "temp_c": 22.0,          # °C
    "hum_pct": 40.0,         # %
    "evap_mmh": 0.5,         # mm/h
    "caudal_cero": 0.2,      # L/min
}

NOMBRES = ["NORMAL", "PRECAUCION", "ALERTA", "CRITICO"]


@dataclass
class ResultadoFusion:
    nivel: int
    puntaje: int
    causas: list[str] = field(default_factory=list)

    @property
    def nombre(self) -> str:
        return NOMBRES[self.nivel]


def _ok(v) -> bool:
    return v is not None and not (isinstance(v, float) and math.isnan(v))


def evaluar(d: dict, u: dict = UMBRALES) -> ResultadoFusion:
    """Calcula el nivel de alerta candidato a partir de una lectura (dict)."""
    puntaje, causas, critico = 0, [], False
    nivel, tasa = d.get("nivel_pct"), d.get("tasa_pct_h", 0.0) or 0.0
    caudal = d.get("caudal_lpm")          # None si el nodo no tiene caudalímetro

    if not _ok(nivel):
        causas.append("falla de sensor de nivel")
    else:
        if nivel < u["nivel_critico"]:
            critico = True
            causas.append("nivel critico")
        if nivel < u["nivel_bajo"]:
            puntaje += 2
            if not critico:
                causas.append("nivel bajo")
        elif nivel < u["nivel_medio"]:
            puntaje += 1
            causas.append("nivel medio")
        if tasa < u["desc_fuerte"]:
            puntaje += 2
            causas.append("descenso rapido")
        elif tasa < u["desc_leve"]:
            puntaje += 1
            causas.append("descenso")
        if _ok(caudal) and tasa < u["desc_leve"] and caudal < u["caudal_cero"]:
            puntaje += 1
            causas.append("posible fuga")

    t, h = d.get("temp_c"), d.get("hum_pct")
    if not (_ok(t) and _ok(h)):
        causas.append("falla de sensor de clima")
    elif t > u["temp_c"] and h < u["hum_pct"]:
        puntaje += 1
        causas.append("calor seco")

    ep = d.get("evap_mmh")
    if _ok(ep) and ep > u["evap_mmh"]:
        puntaje += 1
        causas.append("evaporacion alta")

    if critico or puntaje >= 6:
        n = 3
    elif puntaje >= 4:
        n = 2
    elif puntaje >= 2:
        n = 1
    else:
        n = 0
    return ResultadoFusion(n, puntaje, causas)


class FiltroTemporal:
    """Persistencia + histéresis: evita alertas por lecturas aisladas."""

    def __init__(self, subir: int = 3, bajar: int = 6):
        self.subir, self.bajar = subir, bajar
        self.actual, self._cand, self._cont = 0, 0, 0

    def actualizar(self, candidato: int) -> int:
        if candidato != self._cand:
            self._cand, self._cont = candidato, 0
        if candidato != self.actual:
            self._cont += 1
            necesario = self.subir if candidato > self.actual else self.bajar
            if self._cont >= necesario:
                self.actual, self._cont = candidato, 0
        return self.actual


def evaporacion_pt(temp_c, pres_hpa, rad_wm2):
    """Evaporación potencial horaria (mm/h), Priestley-Taylor."""
    if not all(_ok(x) for x in (temp_c, pres_hpa, rad_wm2)):
        return None
    es = 0.6108 * math.exp(17.27 * temp_c / (temp_c + 237.3))
    delta = 4098 * es / (temp_c + 237.3) ** 2
    gamma = 0.000665 * pres_hpa / 10
    rn = 0.77 * rad_wm2 * 0.0036
    return max(0.0, 1.26 * delta / (delta + gamma) * rn / 2.45)


if __name__ == "__main__":
    # Pruebas rápidas: python3 fusion.py
    assert evaluar({"nivel_pct": 80, "tasa_pct_h": 0, "temp_c": 15, "hum_pct": 70}).nivel == 0
    assert evaluar({"nivel_pct": 10, "temp_c": 15, "hum_pct": 70}).nivel == 3
    r = evaluar({"nivel_pct": 25, "tasa_pct_h": -12, "caudal_lpm": 3,
                 "temp_c": 24, "hum_pct": 35, "evap_mmh": 0.6})
    assert r.nivel == 3 and r.puntaje == 6, r
    r = evaluar({"nivel_pct": 45, "tasa_pct_h": -7, "caudal_lpm": 0,
                 "temp_c": 18, "hum_pct": 60})
    assert r.nivel == 1 and "posible fuga" in r.causas, r
    r = evaluar({"nivel_pct": 45, "tasa_pct_h": -7, "temp_c": 18, "hum_pct": 60})
    assert "posible fuga" not in r.causas, r          # sin caudalímetro no hay regla de fuga
    f = FiltroTemporal()
    assert [f.actualizar(2) for _ in range(3)] == [0, 0, 2]
    print("Ep(22 °C, 750 hPa, 600 W/m2) =", round(evaporacion_pt(22, 750, 600), 3), "mm/h")
    print("fusion.py: todas las pruebas pasaron")
