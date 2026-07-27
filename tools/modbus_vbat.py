#!/usr/bin/env python3
"""Herramienta maestra Modbus RTU para tarjetas buck_adc (modbus_vbat v1.x).

Sin dependencias externas (termios + os). Uso:

  modbus_vbat.py [-p PUERTO] scan [desde] [hasta]
  modbus_vbat.py [-p PUERTO] read ADDR            # lectura completa
  modbus_vbat.py [-p PUERTO] monitor ADDR [ADDR..] [-i SEG]
  modbus_vbat.py [-p PUERTO] set-addr ADDR_ACTUAL ADDR_NUEVA
  modbus_vbat.py [-p PUERTO] set-cal ADDR MV_REAL  # calibra contra multímetro
  modbus_vbat.py [-p PUERTO] set-cal-raw ADDR FACTOR_X10000

Puerto por defecto: /dev/ttyACM0 @ 115200 8N1.
Registros: ver cabecera de modbus_vbat/main.c.
"""

import argparse
import os
import struct
import sys
import termios
import time

BAUD = termios.B115200


def default_port() -> str:
    """Primer adaptador serie USB disponible (portable entre SBCs:
    Orange Pi, Raspberry Pi, Banana Pi... ttyACM* o ttyUSB*)."""
    import glob
    for pat in ("/dev/ttyACM*", "/dev/ttyUSB*"):
        devs = sorted(glob.glob(pat))
        if devs:
            return devs[0]
    return "/dev/ttyACM0"


DEF_PORT = default_port()


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


class Bus:
    def __init__(self, port: str):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
        t = termios.tcgetattr(self.fd)
        t[0] = 0                      # iflag: raw
        t[1] = 0                      # oflag
        t[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag 8N1
        t[3] = 0                      # lflag
        t[4] = t[5] = BAUD
        t[6] = list(t[6])
        t[6][termios.VMIN] = 0
        t[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, t)
        termios.tcflush(self.fd, termios.TCIOFLUSH)

    def xfer_raw(self, req: bytes, timeout: float = 0.25) -> bytes:
        """Envía y devuelve TODO lo recibido, incluso basura de colisión."""
        termios.tcflush(self.fd, termios.TCIFLUSH)
        os.write(self.fd, req + struct.pack("<H", crc16(req)))
        buf = b""
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            chunk = os.read(self.fd, 256)
            if chunk:
                buf += chunk
                last = time.monotonic()
            elif last and time.monotonic() - last > 0.03:
                break
            else:
                time.sleep(0.002)
        return buf

    def xfer(self, req: bytes, timeout: float = 0.25) -> bytes:
        buf = self.xfer_raw(req, timeout)
        if len(buf) < 5 or crc16(buf) != 0:
            return b""
        return buf[:-2]

    def read_regs(self, addr: int, fc: int, start: int, count: int):
        r = self.xfer(struct.pack(">BBHH", addr, fc, start, count))
        if len(r) < 3 or r[0] != addr or r[1] != fc or r[2] != count * 2:
            return None
        return list(struct.unpack(f">{count}H", r[3:3 + count * 2]))

    def write_reg(self, addr: int, reg: int, val: int) -> bool:
        req = struct.pack(">BBHH", addr, 0x06, reg, val)
        r = self.xfer(req)
        return r == req


def fmt_uid(regs):
    return "".join(f"{w:04X}" for w in reversed(regs))


def fmt_uid_bytes(uid12: bytes) -> str:
    words = struct.unpack("<6H", uid12)
    return "".join(f"{w:04X}" for w in reversed(words))


# ---------- auto-descubrimiento (FC usuario 0x41) ----------

def disc_query(bus, nbits: int, prefix: bytes):
    """→ ('silence'|'clean'|'collision', uid12|None)"""
    raw = bus.xfer_raw(bytes([0, 0x41, 0x01, nbits]) + prefix, timeout=0.15)
    if not raw:
        return "silence", None
    if (len(raw) == 19 and crc16(raw) == 0 and
            raw[0] == 247 and raw[1] == 0x41 and raw[2] == 0x01):
        return "clean", raw[3:15]
    return "collision", None


def disc_assign(bus, uid12: bytes, new_addr: int) -> bool:
    req = bytes([0, 0x41, 0x02]) + uid12 + bytes([new_addr])
    raw = bus.xfer_raw(req, timeout=0.4)      # el guardado borra flash
    return (len(raw) == 17 and crc16(raw) == 0 and
            raw[0] == new_addr and raw[1] == 0x41 and raw[2] == 0x02)


def disc_find_one(bus):
    """Desciende el árbol de bits del UID hasta aislar UNA tarjeta."""
    state, uid = disc_query(bus, 0, bytes(12))
    if state == "silence":
        return None
    if state == "clean":
        return uid
    prefix = bytearray(12)
    for depth in range(96):
        descended = False
        for bit in (0, 1):
            if bit:
                prefix[depth // 8] |= 0x80 >> (depth % 8)
            else:
                prefix[depth // 8] &= ~(0x80 >> (depth % 8)) & 0xFF
            state, uid = disc_query(bus, depth + 1, bytes(prefix))
            if state == "clean":
                return uid
            if state == "collision":
                descended = True
                break
        if not descended:
            return None                        # ambas ramas en silencio
    return None


def cmd_discover(bus, assign: bool, start: int):
    print("Buscando tarjetas sin configurar (addr 247)...")
    found, next_addr = [], start
    while True:
        uid = disc_find_one(bus)
        if uid is None:
            break
        label = fmt_uid_bytes(uid)
        if not assign:
            print(f"  UID {label} (sin configurar)")
            # sin asignar no sale del pool: una sola pasada informativa
            found.append(uid)
            break
        while bus.read_regs(next_addr, 0x04, 0, 1) is not None:
            next_addr += 1                     # dirección ocupada, saltar
        if disc_assign(bus, uid, next_addr):
            check = bus.read_regs(next_addr, 0x04, 0, 1)
            v = f", VBAT={check[0] / 1000:.3f}V" if check else ""
            print(f"  UID {label} → addr {next_addr}{v}")
            found.append(uid)
            next_addr += 1
        else:
            print(f"  UID {label}: falló la asignación, reintento...")
    if not found:
        print("  (no hay tarjetas sin configurar en el bus)")
    elif assign:
        print(f"Listo: {len(found)} tarjeta(s) direccionada(s).")


def cmd_factory_reset(bus, addr: int):
    if not bus.write_reg(addr, 0, 247):
        sys.exit(f"addr {addr}: sin respuesta")
    # guardado por broadcast: sin respuesta esperada (todas persisten)
    bus.xfer_raw(struct.pack(">BBHH", 0, 0x06, 2, 0xA55A), timeout=0.1)
    print(f"OK: addr {addr} → 247 (sin configurar, persistido)")


def cmd_scan(bus, lo, hi):
    print(f"Escaneando direcciones {lo}..{hi}...")
    found = []
    for a in range(lo, hi + 1):
        regs = bus.read_regs(a, 0x04, 0, 1)
        if regs is not None:
            v = regs[0] / 1000
            print(f"  addr {a:3d}: VBAT={v:.3f}V")
            found.append(a)
    if not found:
        print("  (ninguna tarjeta respondió)")
    return found


def cmd_read(bus, addr):
    inp = bus.read_regs(addr, 0x04, 0, 10)
    hold = bus.read_regs(addr, 0x03, 0, 2)
    if inp is None or hold is None:
        sys.exit(f"addr {addr}: sin respuesta")
    print(f"addr {addr}:")
    print(f"  VBAT      = {inp[0] / 1000:.3f} V")
    print(f"  raw ADC   = {inp[1]}")
    print(f"  pin ADC   = {inp[2]} mV")
    print(f"  firmware  = v{inp[3] >> 8}.{inp[3] & 0xFF}")
    print(f"  UID       = {fmt_uid(inp[4:10])}")
    print(f"  cal       = {hold[1]} (x10000)")


def cmd_monitor(bus, addrs, interval):
    print("Ctrl-C para salir")
    while True:
        parts = []
        for a in addrs:
            regs = bus.read_regs(a, 0x04, 0, 1)
            parts.append(f"[{a}] {regs[0] / 1000:.3f}V" if regs
                         else f"[{a}] ---")
        print(time.strftime("%H:%M:%S"), "  ".join(parts))
        time.sleep(interval)


def cmd_set_addr(bus, cur, new):
    if not bus.write_reg(cur, 0, new):
        sys.exit(f"addr {cur}: no respondió al cambio de dirección")
    if not bus.write_reg(new, 2, 0xA55A):
        sys.exit(f"addr {new}: respondió al cambio pero falló el guardado")
    print(f"OK: la tarjeta ahora es addr {new} (persistido en flash)")


def cmd_set_cal(bus, addr, real_mv):
    inp = bus.read_regs(addr, 0x04, 0, 1)
    hold = bus.read_regs(addr, 0x03, 1, 1)
    if inp is None or hold is None:
        sys.exit(f"addr {addr}: sin respuesta")
    reported, cal_old = inp[0], hold[0]
    cal_new = round(cal_old * real_mv / reported)
    if not 5000 <= cal_new <= 20000:
        sys.exit(f"factor resultante {cal_new} fuera de rango — ¿mV correctos?")
    if not bus.write_reg(addr, 1, cal_new):
        sys.exit("falló la escritura de calibración")
    if not bus.write_reg(addr, 2, 0xA55A):
        sys.exit("falló el guardado en flash")
    check = bus.read_regs(addr, 0x04, 0, 1)
    print(f"OK: cal {cal_old} → {cal_new} (persistido)")
    print(f"    reportaba {reported / 1000:.3f}V, real {real_mv / 1000:.3f}V, "
          f"ahora lee {check[0] / 1000:.3f}V")


def cmd_set_cal_raw(bus, addr, factor):
    if not bus.write_reg(addr, 1, factor):
        sys.exit("falló la escritura de calibración")
    if not bus.write_reg(addr, 2, 0xA55A):
        sys.exit("falló el guardado en flash")
    print(f"OK: cal={factor} persistido en addr {addr}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-p", "--port", default=DEF_PORT)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("scan")
    s.add_argument("lo", nargs="?", type=int, default=1)
    s.add_argument("hi", nargs="?", type=int, default=247)

    s = sub.add_parser("read")
    s.add_argument("addr", type=int)

    s = sub.add_parser("monitor")
    s.add_argument("addrs", nargs="+", type=int)
    s.add_argument("-i", "--interval", type=float, default=1.0)

    s = sub.add_parser("set-addr")
    s.add_argument("cur", type=int)
    s.add_argument("new", type=int)

    s = sub.add_parser("set-cal")
    s.add_argument("addr", type=int)
    s.add_argument("real_mv", type=int, help="voltaje real en mV (multímetro)")

    s = sub.add_parser("set-cal-raw")
    s.add_argument("addr", type=int)
    s.add_argument("factor", type=int)

    s = sub.add_parser("discover",
                       help="enumera tarjetas sin configurar por UID")
    s.add_argument("--assign", action="store_true",
                   help="asignarles direcciones automáticamente")
    s.add_argument("--start", type=int, default=1,
                   help="primera dirección candidata (def. 1)")

    s = sub.add_parser("factory-reset",
                       help="devuelve una tarjeta al estado sin configurar")
    s.add_argument("addr", type=int)

    a = ap.parse_args()
    bus = Bus(a.port)

    if a.cmd == "scan":
        cmd_scan(bus, a.lo, a.hi)
    elif a.cmd == "read":
        cmd_read(bus, a.addr)
    elif a.cmd == "monitor":
        cmd_monitor(bus, a.addrs, a.interval)
    elif a.cmd == "set-addr":
        cmd_set_addr(bus, a.cur, a.new)
    elif a.cmd == "set-cal":
        cmd_set_cal(bus, a.addr, a.real_mv)
    elif a.cmd == "set-cal-raw":
        cmd_set_cal_raw(bus, a.addr, a.factor)
    elif a.cmd == "discover":
        cmd_discover(bus, a.assign, a.start)
    elif a.cmd == "factory-reset":
        cmd_factory_reset(bus, a.addr)


if __name__ == "__main__":
    main()
