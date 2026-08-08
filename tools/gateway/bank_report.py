#!/usr/bin/env python3
"""Informe de salud del banco a partir de bank.db (ver bank_collector.py).

Por cada día calcula, para cada batería:
  · reposo del alba  — voltaje ya asentado, mide el estado de carga real
  · caída de madrugada (mV/h) — el test de fuga; ventana 01:00-05:00, que
    excluye el colapso de carga superficial de las primeras horas y por eso
    es comparable entre baterías aunque hayan coronado distinto
  · pico del día — cuánto voltaje recibió en absorción
y los contrasta con la energía solar generada ese día, que es lo que
distingue "bajó porque no hubo sol" de "bajó porque fuga".

Uso: bank_report.py [días]
"""

import datetime as dt
import sqlite3
import sys

DB = "/home/orangepi/bank.db"
CARDS = [(2, "150-B"), (3, "200-N"), (4, "150-A"), (5, "200-V")]


def local_date(ts):
    return dt.datetime.fromtimestamp(ts).date()


def at_hour(con, addr, day, hour, ventana=1.5):
    """vavg del registro más cercano a esa hora local, o None."""
    centro = dt.datetime.combine(day, dt.time(hour)).timestamp()
    row = con.execute(
        "SELECT vavg, ts FROM samples WHERE addr=? AND ts BETWEEN ? AND ? "
        "ORDER BY ABS(ts-?) LIMIT 1",
        (addr, centro - ventana * 3600, centro + ventana * 3600, centro)
    ).fetchone()
    return row[0] if row else None


def main():
    dias = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    con = sqlite3.connect(DB)

    # los registros anteriores a la primera ancla tienen hora reconstruida:
    # se marcan con * y no entran en los promedios
    r = con.execute("SELECT MIN(ts) FROM anchors").fetchone()
    desde_fiable = local_date(r[0]) if r and r[0] else None

    fechas = sorted({local_date(r[0]) for r in con.execute(
        "SELECT ts FROM samples")}, reverse=True)[:dias]
    if not fechas:
        print("(sin datos todavía — dejá correr el recolector)")
        return

    print("%-11s %6s | %s" % ("FECHA", "kWh", "  ".join(
        "%-16s" % n for _, n in CARDS)))
    print("%-11s %6s | %s" % ("", "sol", "  ".join(
        "%-16s" % "alba   mV/h  pico" for _ in CARDS)))
    print("-" * (20 + 18 * len(CARDS)))

    caidas = {a: [] for a, _ in CARDS}
    for day in sorted(fechas):
        d0 = dt.datetime.combine(day, dt.time(0)).timestamp()
        d1 = d0 + 86400
        kwh = con.execute(
            "SELECT MAX(gen_kwh_day) FROM epever WHERE ts BETWEEN ? AND ?",
            (d0, d1)).fetchone()[0]
        celdas = []
        for addr, _ in CARDS:
            # ventana profunda 21:00(-1d) → 05:00: ya sin carga superficial
            v21 = at_hour(con, addr, day - dt.timedelta(days=1), 21)
            v5 = at_hour(con, addr, day, 5)
            v1 = v21                       # arranque de la ventana
            alba = at_hour(con, addr, day, 6) or v5
            pico = con.execute(
                "SELECT MAX(vmax) FROM samples WHERE addr=? AND ts BETWEEN ? AND ?",
                (addr, d0 + 8 * 3600, d0 + 19 * 3600)).fetchone()[0]
            fiable = desde_fiable is not None and day > desde_fiable
            if v1 and v5:
                mvh = (v1 - v5) / 8.0
                if fiable:
                    caidas[addr].append(mvh)
            else:
                mvh = None
            celdas.append("%-6s %5s %5s" % (
                "%.3f" % (alba / 1000) if alba else "  -  ",
                "%.0f" % mvh if mvh is not None else " - ",
                "%.2f" % (pico / 1000) if pico else "  -  "))
        marca = "" if (desde_fiable and day > desde_fiable) else "*"
        print("%-10s%1s %6s | %s" % (
            day.isoformat(), marca, "%.2f" % kwh if kwh is not None else "  -  ",
            "  ".join(celdas)))
    if desde_fiable:
        print("(*) hora reconstruida: no entra en los promedios")

    print()
    # reparto de la absorción del último día con datos
    ult = max(fechas)
    d0 = dt.datetime.combine(ult, dt.time(0)).timestamp()
    filas = con.execute(
        "SELECT ts FROM epever WHERE stage='absorcion' AND ts BETWEEN ? AND ?",
        (d0, d0 + 86400)).fetchall()
    if filas:
        t0, t1 = min(f[0] for f in filas), max(f[0] for f in filas)
        print("REPARTO EN ABSORCIÓN del %s (%02d:%02d-%02d:%02d) — quién recibe"
              " el voltaje de carga:" % (ult.isoformat(),
              dt.datetime.fromtimestamp(t0).hour, dt.datetime.fromtimestamp(t0).minute,
              dt.datetime.fromtimestamp(t1).hour, dt.datetime.fromtimestamp(t1).minute))
        for addr, nombre in CARDS:
            r = con.execute("SELECT AVG(vavg), MAX(vmax) FROM samples WHERE addr=?"
                            " AND ts BETWEEN ? AND ?", (addr, t0, t1)).fetchone()
            if r and r[0]:
                marca = "  <-- se satura primero (limita al banco)" if r[0] > 14300 else ""
                print("  %-6s promedio %.3f V  pico %.3f V%s" % (
                    nombre, r[0] / 1000, r[1] / 1000, marca))
        print()
    print("CAÍDA EN REPOSO PROFUNDO 21:00-05:00 (test de fuga; menos es mejor):")
    rank = []
    for addr, nombre in CARDS:
        if caidas[addr]:
            prom = sum(caidas[addr]) / len(caidas[addr])
            rank.append((prom, nombre, len(caidas[addr])))
    for prom, nombre, n in sorted(rank):
        barra = "#" * min(40, int(prom / 2))
        print("  %-6s %5.1f mV/h  (%d noches)  %s" % (nombre, prom, n, barra))
    if len(rank) >= 2:
        peor, mejor = max(rank)[0], min(rank)[0]
        if mejor > 0 and peor / mejor >= 2:
            print("  → %s fuga %.1f× más rápido que %s" % (
                max(rank)[1], peor / mejor, min(rank)[1]))
    # --- vigilancia de las baterías bajo observación ---
    print()
    print("VIGILANCIA (últimos 3 días)")
    desde = (dt.datetime.now() - dt.timedelta(days=3)).timestamp()
    MIN_POR_REG = 13.7          # duración real medida de un registro
    for addr, nombre in CARDS:
        if nombre not in ("150-B", "150-A"):
            continue
        alto = con.execute(
            "SELECT COUNT(*) FROM samples WHERE addr=? AND ts>=? AND vavg>14500",
            (addr, desde)).fetchone()[0]
        riesgo = con.execute(
            "SELECT COUNT(*) FROM samples WHERE addr=? AND ts>=? AND vavg>14800",
            (addr, desde)).fetchone()[0]
        pico = con.execute(
            "SELECT MAX(vmax) FROM samples WHERE addr=? AND ts>=?",
            (addr, desde)).fetchone()[0]
        alertas = con.execute(
            "SELECT tipo, COUNT(*) FROM alerts WHERE addr=? AND ts>=? "
            "GROUP BY tipo", (addr, desde)).fetchall()
        print("  %-6s pico %.3f V | %.0f min sobre 14.5 V | %.0f min sobre 14.8 V%s"
              % (nombre, (pico or 0) / 1000, alto * MIN_POR_REG,
                 riesgo * MIN_POR_REG,
                 ("  [" + ", ".join("%s×%d" % (t_, n) for t_, n in alertas) + "]")
                 if alertas else ""))
        # tendencia del reposo: ¿está recuperando terreno?
        albas = []
        for d in sorted({local_date(r[0]) for r in con.execute(
                "SELECT ts FROM samples WHERE addr=? AND ts>=?", (addr, desde))}):
            v = at_hour(con, addr, d, 6)
            if v:
                albas.append((d, v))
        if len(albas) >= 2:
            delta = albas[-1][1] - albas[0][1]
            print("         reposo del alba: %s  (%+d mV en %d días)" % (
                " → ".join("%.3f" % (v / 1000) for _, v in albas),
                delta, len(albas) - 1))
    con.close()


if __name__ == "__main__":
    main()
