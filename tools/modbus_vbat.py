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

    def write_reg(self, addr: int, reg: int, val: int,
                  timeout: float = 0.25) -> bool:
        req = struct.pack(">BBHH", addr, 0x06, reg, val)
        r = self.xfer(req, timeout)
        return r == req

    def save_cfg(self, addr: int) -> bool:
        """Persistir en flash: el borrado de página puede tardar >250 ms."""
        return self.write_reg(addr, 2, 0xA55A, timeout=1.5)


def fmt_uid(regs):
    return "".join(f"{w:04X}" for w in reversed(regs))


def fmt_uid_bytes(uid12: bytes) -> str:
    words = struct.unpack("<6H", uid12)
    return "".join(f"{w:04X}" for w in reversed(words))


def extract_frame(buf: bytes, addr: int, fc: int):
    """Busca dentro de buf un frame Modbus válido [addr][fc]...[crc16] y lo
    devuelve, ignorando bytes ajenos antes o después (algunos dispositivos
    — EPEVER — responden excepciones a broadcasts y ensucian el bus)."""
    if len(buf) < 5:
        return None
    for start in range(0, len(buf) - 4):
        if buf[start] != addr or buf[start + 1] != fc:
            continue
        for end in range(start + 5, len(buf) + 1):
            if crc16(buf[start:end]) == 0:
                return buf[start:end]
    return None


# ---------- auto-descubrimiento (FC usuario 0x41) ----------

def disc_query(bus, nbits: int, prefix: bytes):
    """→ ('silence'|'clean'|'collision', uid12|None)"""
    raw = bus.xfer_raw(bytes([0, 0x41, 0x01, nbits]) + prefix, timeout=0.15)
    if not raw:
        return "silence", None
    f = extract_frame(raw, 247, 0x41)
    if f is not None and len(f) == 19 and f[2] == 0x01:
        return "clean", f[3:15]
    return "collision", None


def disc_assign(bus, uid12: bytes, new_addr: int) -> bool:
    req = bytes([0, 0x41, 0x02]) + uid12 + bytes([new_addr])
    raw = bus.xfer_raw(req, timeout=0.4)      # el guardado borra flash
    f = extract_frame(raw, new_addr, 0x41)
    return f is not None and len(f) == 17 and f[2] == 0x02


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


RESET_BITS = ["PIN", "POR", "SOFT", "IWDG", "WWDG", "LOWPWR"]


def fmt_reset(cause: int) -> str:
    names = [n for i, n in enumerate(RESET_BITS) if cause & (1 << i)]
    return "+".join(names) if names else "?"


ALARM_BITS = ["BAJA-activa", "ALTA-activa", "BAJA-ocurrió", "ALTA-ocurrió"]


def fmt_alarm(bits: int) -> str:
    names = [n for i, n in enumerate(ALARM_BITS) if bits & (1 << i)]
    return " ".join(names) if names else "sin alarmas"


def cmd_read(bus, addr):
    inp = None
    for count in (21, 14, 11, 10):         # v2.0 / v1.5 / v1.4 / anteriores
        inp = bus.read_regs(addr, 0x04, 0, count)
        if inp is not None:
            break
    hold = bus.read_regs(addr, 0x03, 0, 7) or bus.read_regs(addr, 0x03, 0, 2)
    if inp is None or hold is None:
        sys.exit(f"addr {addr}: sin respuesta")
    print(f"addr {addr}:")
    print(f"  VBAT      = {inp[0] / 1000:.3f} V")
    print(f"  raw ADC   = {inp[1]}")
    print(f"  pin ADC   = {inp[2]} mV")
    print(f"  firmware  = v{inp[3] >> 8}.{inp[3] & 0xFF}")
    print(f"  UID       = {fmt_uid(inp[4:10])}")
    print(f"  cal       = {hold[1]} (x10000)")
    if len(inp) > 10:
        print(f"  últ.reset = {fmt_reset(inp[10])} (0x{inp[10]:02X})")
    if len(inp) > 13:
        print(f"  alarmas   = {fmt_alarm(inp[11])} (0x{inp[11]:02X})")
        print(f"  Vmín/Vmáx = {inp[12] / 1000:.3f} / {inp[13] / 1000:.3f} V")
        if len(hold) > 6:
            lo = f"{hold[4] / 1000:.3f}V" if hold[4] else "off"
            hi = f"{hold[5] / 1000:.3f}V" if hold[5] else "off"
            print(f"  umbrales  = baja {lo}, alta {hi}, hist {hold[6]} mV")
    if len(inp) > 20:
        new = (inp[14] << 16) | inp[15]
        old = (inp[16] << 16) | inp[17]
        upm = (inp[18] << 16) | inp[19]
        print(f"  datalog   = seq {old}..{new} "
              f"({max(0, new - old + 1) if new else 0} registros)")
        print(f"  uptime    = {upm // 1440}d {upm % 1440 // 60}h {upm % 60}m"
              f"   temp chip = {inp[20] - 40}°C")
        hold9 = bus.read_regs(addr, 0x03, 9, 1)
        if hold9:
            print(f"  intervalo = {hold9[0]} min" if hold9[0]
                  else "  intervalo = datalog apagado")


def cmd_set_alarm(bus, addr, lo_mv, hi_mv, hyst):
    for reg, val in ((4, lo_mv), (5, hi_mv)) + \
                    (((6, hyst),) if hyst is not None else ()):
        if not bus.write_reg(addr, reg, val):
            sys.exit(f"addr {addr}: falló escritura reg {reg}")
    if not bus.save_cfg(addr):
        sys.exit("falló el guardado en flash")
    lo = f"{lo_mv / 1000:.3f}V" if lo_mv else "off"
    hi = f"{hi_mv / 1000:.3f}V" if hi_mv else "off"
    print(f"OK addr {addr}: alarma baja {lo}, alta {hi} (persistido)")


def cmd_alarm_clear(bus, addr):
    if not bus.write_reg(addr, 7, 1):
        sys.exit(f"addr {addr}: sin respuesta")
    print(f"OK addr {addr}: latches limpiados, Vmín/Vmáx reiniciados")


LOG_REC = struct.Struct("<IIHHHBB")


def cmd_log(bus, addr, since):
    inp = bus.read_regs(addr, 0x04, 14, 6)
    if inp is None:
        sys.exit(f"addr {addr}: sin respuesta (¿firmware v2.0+?)")
    newest = (inp[0] << 16) | inp[1]
    oldest = (inp[2] << 16) | inp[3]
    upnow  = (inp[4] << 16) | inp[5]
    if newest == 0:
        print("(datalog vacío)")
        return
    start = since if since is not None else oldest
    print(f"registros {start}..{newest}  (uptime actual "
          f"{upnow // 60}h {upnow % 60}m):")
    print(f"{'seq':>6} {'hace':>10} {'vmin':>7} {'vavg':>7} {'vmax':>7} "
          f"{'temp':>5}  flags")
    now = time.time()
    cursor = start
    while cursor <= newest:
        req = struct.pack(">BBIB", addr, 0x43, cursor, 12)
        for retry in range(3):
            raw = bus.xfer_raw(req, timeout=0.5)
            if (len(raw) >= 5 and crc16(raw) == 0 and
                    raw[0] == addr and raw[1] == 0x43):
                break
        else:
            sys.exit(f"error leyendo desde seq {cursor} tras 3 intentos")
        n = raw[2]
        if n == 0:
            break
        for i in range(n):
            seq, upm, vmin, vmax, vavg, flags, temp =                 LOG_REC.unpack(raw[3 + i * 16:3 + (i + 1) * 16])
            ago_min = upnow - upm
            ago = (f"{ago_min // 1440}d{ago_min % 1440 // 60:02d}h"
                   if ago_min >= 1440 else f"{ago_min // 60}h{ago_min % 60:02d}m")
            fl = []
            if flags & 1: fl.append("ALARMA-BAJA")
            if flags & 2: fl.append("ALARMA-ALTA")
            if flags & 4: fl.append(f"BOOT({fmt_reset(flags >> 4)})")
            print(f"{seq:>6} {ago:>10} {vmin / 1000:>6.3f}V {vavg / 1000:>6.3f}V "
                  f"{vmax / 1000:>6.3f}V {temp - 40:>4}°C  {' '.join(fl)}")
            cursor = seq + 1
    print(f"({cursor - start} registros leídos)")


# ---------- actualización de firmware por el bus (boot485, FC 0x42) ----------

def bl_cmd(bus, sub: int, uid: bytes, payload: bytes = b"",
           timeout: float = 0.4):
    """Comando al bootloader; devuelve el payload de la respuesta o None."""
    raw = bus.xfer_raw(bytes([0, 0x42, sub]) + uid + payload, timeout)
    f = extract_frame(raw, 0xF8, 0x42)
    if (f is not None and len(f) >= 17 and f[2] == sub and f[3:15] == uid):
        return f[15:-2]
    return None


def parse_uid(disp: str) -> bytes:
    """UID en formato de display (24 hex, words invertidas) → 12 bytes raw."""
    disp = disp.strip().upper()
    if len(disp) != 24:
        sys.exit("UID debe tener 24 caracteres hex")
    words = [int(disp[i:i + 4], 16) for i in range(0, 24, 4)]
    return struct.pack("<6H", *reversed(words))


def cmd_update(bus, addr, path, uid_hex=None):
    import zlib
    fw = open(path, "rb").read()
    if len(fw) % 2:
        fw += b"\xff"
    if len(fw) > 0x7FF0:
        sys.exit(f"binario de {len(fw)}B excede la zona de app (32K-16)")
    crc = zlib.crc32(fw) & 0xFFFFFFFF
    print(f"Firmware: {len(fw)} bytes, CRC32 {crc:08X}")

    if uid_hex:
        uid = parse_uid(uid_hex)
        print(f"Modo rescate: directo al bootloader, UID {fmt_uid_bytes(uid)}")
    else:
        regs = bus.read_regs(addr, 0x04, 3, 7)
        if regs is None:
            sys.exit(f"addr {addr}: sin respuesta — si quedó en bootloader "
                     f"(LED muy rápido), usá: update {addr} fw.bin --uid <UID>")
        ver, uid = regs[0], struct.pack("<6H", *regs[1:7])
        print(f"Tarjeta addr {addr}: firmware v{ver >> 8}.{ver & 0xFF}, "
              f"UID {fmt_uid_bytes(uid)}")

        print("Reiniciando en modo bootloader...")
        bus.write_reg(addr, 8, 0xB007)
        time.sleep(0.2)

    for _ in range(25):
        r = bl_cmd(bus, 0x00, uid, timeout=0.2)
        if r is not None:
            blver = (r[0] << 8) | r[1]
            print(f"Bootloader v{blver >> 8}.{blver & 0xFF} respondiendo "
                  f"(app previa {'válida' if r[2] else 'inválida'})")
            break
        time.sleep(0.1)
    else:
        sys.exit("el bootloader no respondió — apagá/encendé y reintentá "
                 "(la app anterior sigue intacta)")

    print("Borrando zona de aplicación...")
    for retry in range(5):
        if bl_cmd(bus, 0x01, uid, timeout=3.0) is not None:
            break
    else:
        sys.exit("falló el borrado tras 5 intentos")

    print(f"Enviando {(len(fw) + 63) // 64} bloques", end="", flush=True)
    for off in range(0, len(fw), 64):
        chunk = fw[off:off + 64]
        payload = struct.pack(">IB", off, len(chunk)) + chunk
        for retry in range(6):
            r = bl_cmd(bus, 0x02, uid, payload)
            if r is not None:
                break
        else:
            sys.exit(f"\nfalló la escritura en offset {off} tras 6 intentos")
        if (off // 64) % 20 == 0:
            print(".", end="", flush=True)
    print(" ok")

    trailer = struct.pack("<III", 0xA5B007A5, len(fw), crc) + b"\xff" * 4
    payload = struct.pack(">IB", 0x7FF0, 16) + trailer
    for retry in range(6):
        if bl_cmd(bus, 0x02, uid, payload) is not None:
            break
    else:
        sys.exit("falló la escritura del trailer")

    print("Verificando CRC y activando...")
    for retry in range(4):
        if bl_cmd(bus, 0x03, uid, struct.pack(">II", len(fw), crc),
                  timeout=2.0) is not None:
            break
    else:
        sys.exit("el CRC no cuadró tras 4 intentos — app NO activada; reintentá")

    print("Reiniciando a la aplicación nueva...")
    for retry in range(3):
        if bl_cmd(bus, 0x04, uid, timeout=0.4) is not None:
            break
    time.sleep(0.8)

    for _ in range(10):
        regs = bus.read_regs(addr, 0x04, 0, 4)
        if regs is not None:
            print(f"¡Actualizada! addr {addr} viva con firmware "
                  f"v{regs[3] >> 8}.{regs[3] & 0xFF}, "
                  f"VBAT={regs[0] / 1000:.3f}V (config preservada)")
            return
        time.sleep(0.3)
    sys.exit("la app nueva no respondió — revisar (¿dirección cambió?)")


def cmd_hang_test(bus, addr):
    print(f"Enviando cuelgue intencional a addr {addr}...")
    if not bus.write_reg(addr, 3, 0xDEAD):
        sys.exit(f"addr {addr}: sin respuesta")
    print("Firmware colgado (LED fijo). Esperando reset por IWDG (~7 s)...")
    t0 = time.monotonic()
    while time.monotonic() - t0 < 15:
        time.sleep(1)
        regs = bus.read_regs(addr, 0x04, 10, 1)
        if regs is not None:
            dt = time.monotonic() - t0
            print(f"¡Revivió a los {dt:.1f} s!  causa del reset: "
                  f"{fmt_reset(regs[0])} (0x{regs[0]:02X})")
            return
    sys.exit("no revivió en 15 s — revisar")


def cmd_monitor(bus, addrs, interval):
    print("Ctrl-C para salir")
    while True:
        parts = []
        for a in addrs:
            regs = bus.read_regs(a, 0x04, 0, 12)   # v1.5: con alarmas
            if regs is None:
                regs = bus.read_regs(a, 0x04, 0, 1)
            if regs is None:
                parts.append(f"[{a}] ---")
                continue
            s = f"[{a}] {regs[0] / 1000:.3f}V"
            if len(regs) > 11 and regs[11] & 0x03:
                s += " ⚠" + ("BAJA" if regs[11] & 1 else "ALTA")
            parts.append(s)
        print(time.strftime("%H:%M:%S"), "  ".join(parts))
        time.sleep(interval)


def cmd_set_addr(bus, cur, new):
    if not bus.write_reg(cur, 0, new):
        sys.exit(f"addr {cur}: no respondió al cambio de dirección")
    if not bus.save_cfg(new):
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
    if not bus.save_cfg(addr):
        sys.exit("falló el guardado en flash")
    check = bus.read_regs(addr, 0x04, 0, 1) or bus.read_regs(addr, 0x04, 0, 1)
    print(f"OK: cal {cal_old} → {cal_new} (persistido)")
    now = f"{check[0] / 1000:.3f}V" if check else "(verificar con read)"
    print(f"    reportaba {reported / 1000:.3f}V, real {real_mv / 1000:.3f}V, "
          f"ahora lee {now}")


def cmd_set_cal_raw(bus, addr, factor):
    if not bus.write_reg(addr, 1, factor):
        sys.exit("falló la escritura de calibración")
    if not bus.save_cfg(addr):
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

    s = sub.add_parser("hang-test",
                       help="cuelga el firmware a propósito y verifica "
                            "que el watchdog lo resucite")
    s.add_argument("addr", type=int)

    s = sub.add_parser("set-alarm",
                       help="umbrales de alarma en mV (0 = deshabilitar)")
    s.add_argument("addr", type=int)
    s.add_argument("lo_mv", type=int, help="umbral bajo en mV (0=off)")
    s.add_argument("hi_mv", type=int, help="umbral alto en mV (0=off)")
    s.add_argument("--hyst", type=int, default=None,
                   help="histéresis en mV (def. actual de la tarjeta)")

    s = sub.add_parser("alarm-clear",
                       help="limpia latches y reinicia Vmín/Vmáx")
    s.add_argument("addr", type=int)

    s = sub.add_parser("update",
                       help="actualiza el firmware por RS-485 (boot485)")
    s.add_argument("addr", type=int)
    s.add_argument("file", help="binario de la app (modbus_vbat.bin)")
    s.add_argument("--uid", default=None,
                   help="rescate: UID (24 hex) de tarjeta ya en bootloader")

    s = sub.add_parser("log", help="lee el datalogger de la tarjeta (v2.0+)")
    s.add_argument("addr", type=int)
    s.add_argument("--since", type=int, default=None,
                   help="desde esta seq (def. todo)")

    s = sub.add_parser("set-log-interval",
                       help="intervalo del datalogger en minutos (0=off)")
    s.add_argument("addr", type=int)
    s.add_argument("minutes", type=int)

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
    elif a.cmd == "hang-test":
        cmd_hang_test(bus, a.addr)
    elif a.cmd == "set-alarm":
        cmd_set_alarm(bus, a.addr, a.lo_mv, a.hi_mv, a.hyst)
    elif a.cmd == "alarm-clear":
        cmd_alarm_clear(bus, a.addr)
    elif a.cmd == "update":
        cmd_update(bus, a.addr, a.file, a.uid)
    elif a.cmd == "log":
        cmd_log(bus, a.addr, a.since)
    elif a.cmd == "set-log-interval":
        if not bus.write_reg(a.addr, 9, a.minutes):
            sys.exit("falló la escritura del intervalo")
        if not bus.save_cfg(a.addr):
            sys.exit("falló el guardado")
        print(f"OK: intervalo datalog = {a.minutes} min (persistido)")


if __name__ == "__main__":
    main()
