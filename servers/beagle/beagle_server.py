#!/usr/bin/env python3
"""Servidor TCP de la BeagleBone.

Recibe ordenes por lineas y controla el canal de luz:
  clock : gpio44 (salida)
  dato  : gpio60 (LED, salida)

Comandos:
  PING              -> PONG
  BITS <secuencia>  -> OK BITS <n>
  QUIT              -> BYE

Compatible con Python 3.5.3 (Debian Stretch).
"""

import argparse
import errno
import os
import socket
import time

SYSFS_GPIO = "/sys/class/gpio"
CLOCK_GPIO = 44
DATA_GPIO = 60
CLOCK_HZ = 2.0
MAX_BITS = 128


def sysfs_write(path, value):
    with open(path, "w") as handle:
        handle.write(str(value))


def valid_bits(text):
    if not text or len(text) > MAX_BITS:
        return False
    for char in text:
        if char != "0" and char != "1":
            return False
    return True


def format_bytes(bits):
    groups = []
    for index in range(0, len(bits), 8):
        groups.append(bits[index:index + 8])
    return " ".join(groups)


class GpioChannel:
    def __init__(self, clock_gpio, data_gpio):
        self.clock_gpio = clock_gpio
        self.data_gpio = data_gpio
        self._exported = []
        self._clock_path = None
        self._data_path = None

    def _open_pin(self, number):
        path = os.path.join(SYSFS_GPIO, "gpio{}".format(number))
        if not os.path.isdir(path):
            try:
                sysfs_write(os.path.join(SYSFS_GPIO, "export"), number)
            except OSError as exc:
                if exc.errno != errno.EBUSY:
                    raise
            for _ in range(20):
                if os.path.isdir(path):
                    break
                time.sleep(0.01)
            else:
                raise OSError("gpio{} no aparecio en sysfs".format(number))
            self._exported.append(number)
        sysfs_write(os.path.join(path, "direction"), "out")
        sysfs_write(os.path.join(path, "value"), 0)
        return os.path.join(path, "value")

    def open(self):
        self._clock_path = self._open_pin(self.clock_gpio)
        self._data_path = self._open_pin(self.data_gpio)

    def _write(self, path, value):
        sysfs_write(path, 1 if value else 0)

    def send_bits(self, bits, clock_hz):
        half_seconds = 1.0 / clock_hz
        high_seconds = half_seconds * 0.4
        low_seconds = half_seconds - high_seconds
        for bit in bits:
            self._write(self._data_path, 1 if bit == "1" else 0)
            for _ in range(2):
                self._write(self._clock_path, 1)
                time.sleep(high_seconds)
                self._write(self._clock_path, 0)
                time.sleep(low_seconds)
        self._write(self._data_path, 0)
        self._write(self._clock_path, 0)

    def close(self):
        try:
            if self._data_path is not None:
                self._write(self._data_path, 0)
            if self._clock_path is not None:
                self._write(self._clock_path, 0)
        except OSError:
            pass
        for number in self._exported:
            try:
                sysfs_write(os.path.join(SYSFS_GPIO, "unexport"), number)
            except OSError:
                pass
        self._exported = []
        self._clock_path = None
        self._data_path = None


class DryChannel:
    def send_bits(self, bits, clock_hz):
        print("[beagle] dry-run BITS {} ({:.2f} Hz)".format(bits, clock_hz), flush=True)

    def close(self):
        pass


class ChannelController:
    def __init__(self, clock_gpio, data_gpio, dry_run):
        self.clock_gpio = clock_gpio
        self.data_gpio = data_gpio
        self.dry_run = dry_run
        self.channel = DryChannel() if dry_run else None

    def send(self, bits, clock_hz):
        if self.channel is None:
            channel = GpioChannel(self.clock_gpio, self.data_gpio)
            try:
                channel.open()
            except OSError as exc:
                return False, "gpio {}".format(exc)
            self.channel = channel
        self.channel.send_bits(bits, clock_hz)
        return True, None

    def close(self):
        if self.channel is not None:
            self.channel.close()
            self.channel = None


def process_command(line, controller, clock_hz):
    command = line.upper()
    if command == "PING":
        return "PONG", False
    if command == "QUIT":
        return "BYE", True
    if command.startswith("BITS "):
        bits = line[5:].strip()
        if not valid_bits(bits):
            return "ERR invalid bits", False
        print("[beagle] {} bytes para LED: {}".format(
            (len(bits) + 7) // 8, format_bytes(bits)), flush=True)
        ok, error = controller.send(bits, clock_hz)
        if not ok:
            return "ERR {}".format(error), False
        return "OK BITS {}".format(len(bits)), False
    return "ERR unknown", False


def handle_connection(conn, addr, controller, clock_hz):
    print("[beagle] cliente {}:{}".format(addr[0], addr[1]), flush=True)
    buffer = b""
    try:
        while True:
            data = conn.recv(1024)
            if not data:
                break
            buffer += data
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").strip()
                if not line:
                    continue
                response, quit_requested = process_command(line, controller, clock_hz)
                conn.sendall((response + "\n").encode("utf-8"))
                if quit_requested:
                    return
    except OSError as exc:
        print("[beagle] error con {}:{}: {}".format(addr[0], addr[1], exc), flush=True)
    finally:
        conn.close()
        print("[beagle] desconectado {}:{}".format(addr[0], addr[1]), flush=True)


def serve(host, port, controller, clock_hz):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((host, port))
    server.listen(5)
    print("[beagle] escuchando en {}:{}".format(host, port), flush=True)
    try:
        while True:
            conn, addr = server.accept()
            handle_connection(conn, addr, controller, clock_hz)
    except KeyboardInterrupt:
        print("\n[beagle] detenido", flush=True)
    finally:
        controller.close()
        server.close()


def main():
    parser = argparse.ArgumentParser(description="Endpoint TCP de la BeagleBone")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=7000)
    parser.add_argument("--clock-gpio", type=int, default=CLOCK_GPIO)
    parser.add_argument("--data-gpio", type=int, default=DATA_GPIO)
    parser.add_argument("--clock-hz", type=float, default=CLOCK_HZ)
    parser.add_argument("--dry-run", action="store_true",
                        help="no usa GPIO; solo valida el protocolo")
    args = parser.parse_args()

    if args.clock_hz < 0.1 or args.clock_hz > 20.0:
        parser.error("--clock-hz debe estar entre 0.1 y 20.0")

    controller = ChannelController(args.clock_gpio, args.data_gpio, args.dry_run)
    serve(args.host, args.port, controller, args.clock_hz)


if __name__ == "__main__":
    main()
