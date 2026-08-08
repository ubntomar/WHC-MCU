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
            v1 = at_hour(con, addr, day, 1)
            v5 = at_hour(con, addr, day, 5)
            alba = at_hour(con, addr, day, 6) or v5
            pico = con.execute(
                "SELECT MAX(vmax) FROM samples WHERE addr=? AND ts BETWEEN ? AND ?",
                (addr, d0 + 8 * 3600, d0 + 19 * 3600)).fetchone()[0]
            if v1 and v5:
                mvh = (v1 - v5) / 4.0
                caidas[addr].append(mvh)
            else:
                mvh = None
            celdas.append("%-6s %5s %5s" % (
                "%.3f" % (alba / 1000) if alba else "  -  ",
                "%.0f" % mvh if mvh is not None else " - ",
                "%.2f" % (pico / 1000) if pico else "  -  "))
        print("%-11s %6s | %s" % (
            day.isoformat(), "%.2f" % kwh if kwh is not None else "  -  ",
            "  ".join(celdas)))

    print()
    print("PROMEDIO DE CAÍDA DE MADRUGADA (test de fuga; menos es mejor):")
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
    con.close()


if __name__ == "__main__":
    main()
