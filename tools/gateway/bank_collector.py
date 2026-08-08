#!/usr/bin/env python3
"""Recolector del banco de baterías → SQLite. Prototipo del gateway.

Corre por cron cada 15 min. Hace tres cosas:
  1. Recolecta incrementalmente el datalog de cada tarjeta (por número de
     secuencia) y lo acumula en una base local, de modo que el historial
     sobrevive al anillo de ~9 días de la flash.
  2. Le pone hora real a cada registro. Las tarjetas no tienen reloj de
     pared a propósito: se interpola entre el instante de esta recolección
     y el de la anterior, así que recolectar seguido = timestamps precisos
     (y el desfase del reloj interno de la tarjeta deja de importar).
  3. Guarda una instantánea del EPEVER (etapa, potencia, energía del día).

Sin dependencias externas: solo stdlib + modbus_vbat.py.
"""

import fcntl
import os
import sqlite3
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "/home/orangepi")
from modbus_vbat import Bus, extract_frame, LOG_REC, default_port

DB = os.environ.get("BANK_DB", "/home/orangepi/bank.db")
LOCK = "/tmp/bank_collector.lock"
EPEVER_ADDR = 1

# addr → (etiqueta de la batería, capacidad Ah)
CARDS = {
    2: ("150-B", 150),
    3: ("200-N", 200),
    4: ("150-A", 150),
    5: ("200-V", 200),
}

SCHEMA = """
CREATE TABLE IF NOT EXISTS samples (
    addr    INTEGER NOT NULL,
    seq     INTEGER NOT NULL,
    ts      REAL    NOT NULL,      -- hora real estimada (epoch)
    vmin    INTEGER, vmax INTEGER, vavg INTEGER,
    flags   INTEGER, temp INTEGER,
    uptime  INTEGER,
    PRIMARY KEY (addr, seq)
);
CREATE INDEX IF NOT EXISTS ix_samples_ts ON samples(ts);

CREATE TABLE IF NOT EXISTS epever (
    ts          REAL PRIMARY KEY,
    stage       TEXT,
    pv_w        REAL, batt_a REAL, bank_v REAL,
    gen_kwh_day REAL, batt_temp REAL,
    vmax_day    REAL, vmin_day REAL
);

-- (ts, seq más nuevo) por recolección: verdad de terreno para medir cuánto
-- dura de verdad un intervalo del datalogger (el reloj de la tarjeta deriva)
CREATE TABLE IF NOT EXISTS anchors (
    addr INTEGER NOT NULL,
    ts   REAL    NOT NULL,
    seq  INTEGER NOT NULL,
    PRIMARY KEY (addr, ts)
);

-- avisos automáticos (vigilancia de las baterías bajo observación)
CREATE TABLE IF NOT EXISTS alerts (
    ts    REAL NOT NULL,
    addr  INTEGER NOT NULL,
    tipo  TEXT NOT NULL,
    valor REAL,
    PRIMARY KEY (ts, addr, tipo)
);

CREATE TABLE IF NOT EXISTS live (
    ts   REAL NOT NULL,
    addr INTEGER NOT NULL,
    v    REAL,
    PRIMARY KEY (ts, addr)
);
"""


def rd(bus, addr, start, count, fc=4):
    for _ in range(6):
        raw = bus.xfer_raw(struct.pack(">BBHH", addr, fc, start, count), 0.5)
        f = extract_frame(raw, addr, fc)
        if f and len(f) == 5 + 2 * count:
            return [(f[3 + 2 * i] << 8) | f[4 + 2 * i] for i in range(count)]
    return None


def get_log(bus, addr, desde, hasta, tope=400):
    """Registros [desde..hasta] del datalog, en orden de secuencia."""
    recs, cursor, intentos = [], desde, 0
    while cursor <= hasta and len(recs) < tope and intentos < 3 * tope:
        intentos += 1
        raw = bus.xfer_raw(struct.pack(">BBIB", addr, 0x43, cursor, 6), 0.6)
        f = extract_frame(raw, addr, 0x43)
        if f and len(f) >= 5 and len(f) == 5 + f[2] * 16:
            if f[2] == 0:
                break
            for i in range(f[2]):
                recs.append(LOG_REC.unpack(f[3 + i * 16:3 + (i + 1) * 16]))
            cursor = recs[-1][0] + 1
    return recs


# Umbrales de vigilancia (mV). El gel no recombina el gas: por encima de
# ~14.8 V por batería cada minuto cuenta como desgaste.
V_ATENCION = 14500
V_RIESGO   = 14800


def revisar(con, addr, filas):
    """Deja constancia de picos altos y de alarmas de las tarjetas."""
    for (_a, _seq, ts, _vmin, vmax, vavg, flags, _t, _up) in filas:
        if vmax >= V_RIESGO:
            con.execute("INSERT OR IGNORE INTO alerts VALUES (?,?,?,?)",
                        (ts, addr, "pico_riesgo", vmax / 1000))
        elif vmax >= V_ATENCION:
            con.execute("INSERT OR IGNORE INTO alerts VALUES (?,?,?,?)",
                        (ts, addr, "pico_atencion", vmax / 1000))
        if flags & 0x01:
            con.execute("INSERT OR IGNORE INTO alerts VALUES (?,?,?,?)",
                        (ts, addr, "alarma_baja", vavg / 1000))
        if flags & 0x02:
            con.execute("INSERT OR IGNORE INTO alerts VALUES (?,?,?,?)",
                        (ts, addr, "alarma_alta", vavg / 1000))


def collect_card(bus, con, addr, now):
    regs = rd(bus, addr, 14, 2)
    if regs is None:
        return 0, None
    newest = (regs[0] << 16) | regs[1]
    if newest == 0:
        return 0, None

    row = con.execute(
        "SELECT seq, ts FROM samples WHERE addr=? ORDER BY seq DESC LIMIT 1",
        (addr,)).fetchone()
    prev_seq, prev_ts = row if row else (None, None)

    desde = prev_seq + 1 if prev_seq is not None else max(1, newest - 300)
    if desde > newest:
        return 0, newest

    recs = get_log(bus, addr, desde, newest)
    if not recs:
        return 0, newest

    # hora real por interpolación entre la recolección previa y ahora
    span_seq = newest - prev_seq if prev_seq is not None else None
    span_t = now - prev_ts if prev_ts is not None else None
    filas = []
    for seq, uptime, vmin, vmax, vavg, flags, temp in recs:
        if span_seq and span_seq > 0 and span_t and span_t < 6 * 3600:
            ts = prev_ts + span_t * (seq - prev_seq) / span_seq
        else:
            # sin referencia fiable: estimar hacia atrás a 10 min por registro
            ts = now - (newest - seq) * 600
        filas.append((addr, seq, ts, vmin, vmax, vavg, flags, temp, uptime))

    revisar(con, addr, filas)
    con.executemany(
        "INSERT OR IGNORE INTO samples "
        "(addr,seq,ts,vmin,vmax,vavg,flags,temp,uptime) "
        "VALUES (?,?,?,?,?,?,?,?,?)", filas)
    return len(filas), newest


def collect_epever(bus, con, now):
    st = rd(bus, EPEVER_ADDR, 0x3201, 1)
    pv = rd(bus, EPEVER_ADDR, 0x3100, 6)
    gen = rd(bus, EPEVER_ADDR, 0x330C, 2)
    hist = rd(bus, EPEVER_ADDR, 0x3300, 4)
    tb = rd(bus, EPEVER_ADDR, 0x3110, 1)
    if st is None and pv is None:
        return False
    stage = {0: "sin_carga", 1: "flotacion", 2: "absorcion",
             3: "ecualizacion"}.get((st[0] >> 2) & 3) if st else None
    con.execute(
        "INSERT OR REPLACE INTO epever "
        "(ts,stage,pv_w,batt_a,bank_v,gen_kwh_day,batt_temp,vmax_day,vmin_day)"
        " VALUES (?,?,?,?,?,?,?,?,?)",
        (now, stage,
         (((pv[3] << 16) | pv[2]) / 100) if pv else None,
         (pv[5] / 100) if pv else None,
         (pv[4] / 100) if pv else None,
         (((gen[1] << 16) | gen[0]) / 100) if gen else None,
         (tb[0] / 100) if tb else None,
         (hist[2] / 100) if hist else None,
         (hist[3] / 100) if hist else None))
    return True


def sec_per_seq(con, addr, min_horas=1.0):
    """Segundos reales por registro del datalog, medidos entre anclas."""
    filas = con.execute(
        "SELECT ts, seq FROM anchors WHERE addr=? ORDER BY ts", (addr,)).fetchall()
    if len(filas) < 2:
        return None
    (t0, s0), (t1, s1) = filas[0], filas[-1]
    if s1 <= s0 or (t1 - t0) < min_horas * 3600:
        return None
    return (t1 - t0) / (s1 - s0)


def restamp(con):
    """Corrige la hora de los registros rescatados antes de tener anclas."""
    total = 0
    for addr in CARDS:
        sps = sec_per_seq(con, addr)
        if not sps:
            print("  addr %d: aún sin anclas suficientes (dejá correr el cron)"
                  % addr)
            continue
        fila = con.execute("SELECT MIN(ts), seq FROM anchors WHERE addr=? "
                           "GROUP BY addr", (addr,)).fetchone()
        anc_ts = con.execute("SELECT ts, seq FROM anchors WHERE addr=? "
                             "ORDER BY ts LIMIT 1", (addr,)).fetchone()
        if not anc_ts:
            continue
        base_ts, base_seq = anc_ts
        cur = con.execute(
            "UPDATE samples SET ts = ? - (? - seq) * ? "
            "WHERE addr=? AND seq <= ? AND ts <= ?",
            (base_ts, base_seq, sps, addr, base_seq, base_ts))
        total += cur.rowcount
        print("  addr %d: %.1f min reales por registro (config: 10) — "
              "%d registros re-estampados" % (addr, sps / 60, cur.rowcount))
    con.commit()
    return total


def main():
    if "--restamp" in sys.argv:
        con = sqlite3.connect(DB)
        con.executescript(SCHEMA)
        print("Re-estampando historial con la deriva medida:")
        restamp(con)
        con.close()
        return 0

    lock = open(LOCK, "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        return 0                      # ya hay una recolección en curso

    port = default_port()
    if not os.path.exists(port):
        print("%s  sin puerto serie (%s)" % (time.strftime("%F %T"), port))
        return 1

    con = sqlite3.connect(DB)
    con.executescript(SCHEMA)
    now = time.time()
    bus = Bus(port)

    resumen = []
    for addr, (nombre, _cap) in sorted(CARDS.items()):
        n, newest = collect_card(bus, con, addr, now)
        v = rd(bus, addr, 0, 1)
        if v:
            con.execute("INSERT OR REPLACE INTO live (ts,addr,v) VALUES (?,?,?)",
                        (now, addr, v[0] / 1000))
        resumen.append("%s:%s%s" % (nombre, "+%d" % n if n else "0",
                                    "" if v else "(muda)"))
    ep = collect_epever(bus, con, now)
    for addr in CARDS:
        r = rd(bus, addr, 14, 2)
        if r:
            con.execute("INSERT OR REPLACE INTO anchors (addr,ts,seq) "
                        "VALUES (?,?,?)", (addr, now, (r[0] << 16) | r[1]))
    con.commit()
    con.close()
    print("%s  %s  epever:%s" % (time.strftime("%F %T"), " ".join(resumen),
                                 "ok" if ep else "sin_respuesta"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
