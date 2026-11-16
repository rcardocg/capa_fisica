#!/usr/bin/env python3
"""Prueba integral de software de la fase 5."""

import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCAL_SERVER = os.path.join(ROOT, "servers", "local", "local_server.py")


class LineCollector:
    def __init__(self, stream):
        self.stream = stream
        self.lines = []
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self.run)
        self.thread.daemon = True
        self.thread.start()

    def run(self):
        try:
            for line in self.stream:
                line = line.rstrip("\n")
                with self.lock:
                    self.lines.append(line)
        except (OSError, ValueError):
            pass

    def mark(self):
        with self.lock:
            return len(self.lines)

    def wait_for(self, needle, timeout, start=0):
        deadline = time.time() + timeout
        index = start
        while time.time() < deadline:
            with self.lock:
                while index < len(self.lines):
                    line = self.lines[index]
                    index += 1
                    if needle in line:
                        return line
            time.sleep(0.02)
        return None


class NodeEvents:
    def __init__(self, port):
        self.port = port
        self.since = 0

    def poll(self):
        lines = node_command(self.port, "POLL {}".format(self.since), multi=True)
        messages = []
        next_index = self.since
        for line in lines:
            if line.startswith("NEXT "):
                try:
                    next_index = int(line[5:].strip())
                except ValueError:
                    pass
            elif line.startswith("MESSAGE "):
                parts = line.split(" ", 3)
                if len(parts) == 4:
                    messages.append({
                        "id": parts[1],
                        "user": parts[2],
                        "text": parts[3],
                    })
        self.since = next_index
        return messages

    def wait_message(self, user, text, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for message in self.poll():
                if message["user"] == user and message["text"] == text:
                    return message
            time.sleep(0.02)
        return None


def free_port():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def node_command(port, command, multi=False, timeout=5.0):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall((command + "\n").encode("utf-8"))
        stream = sock.makefile("r", encoding="utf-8", errors="replace")
        try:
            if multi:
                lines = []
                while True:
                    raw = stream.readline()
                    if not raw:
                        break
                    line = raw.rstrip("\n")
                    lines.append(line)
                    if line.startswith("NEXT "):
                        break
                return lines
            raw = stream.readline()
            return [raw.rstrip("\n")] if raw else []
        finally:
            stream.close()


def http_json(method, url, payload=None, timeout=5.0):
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return exc.code, json.loads(exc.read().decode("utf-8"))


def wait_http_ok(url, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            status, data = http_json("GET", url)
            if status == 200:
                return data
        except (OSError, ValueError):
            pass
        time.sleep(0.1)
    return None


def wait_port(port, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.05)
    return False


def wait_inbox_message(base_url, since, user, text, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        status, data = http_json("GET", "{}/api/inbox?since={}".format(base_url, since))
        if status != 200:
            return since, None
        messages = data.get("messages", [])
        for message in messages:
            if message.get("user") == user and message.get("text") == text:
                return data.get("next", since), message
        since = data.get("next", since)
        time.sleep(0.05)
    return since, None


def crc8_smbus(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def frame_bits(control, seq, data, corrupt=False):
    crc = crc8_smbus([((control & 0x0F) << 4) | (seq & 0x0F), data & 0xFF])
    if corrupt:
        crc ^= 0x01
    fields = [(0xAAAA, 16), (0x0D, 4), (control, 4), (seq, 4), (data, 8), (crc, 8)]
    return "".join(format(value, "0{}b".format(width)) for value, width in fields)


def terminate(process):
    if process is None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def main():
    parser = argparse.ArgumentParser(description="Prueba integral de la fase 5")
    parser.add_argument("--sim", default=os.path.join(ROOT, "c", "phy_sim"))
    parser.add_argument("--clock-hz", type=int, default=1000)
    args = parser.parse_args()

    sim_path = os.path.abspath(args.sim)
    if not os.path.isfile(sim_path) or not os.access(sim_path, os.X_OK):
        print("FALLO: no se encontro el simulador {}".format(sim_path))
        return 1

    a_port = free_port()
    b_port = free_port()
    local_port = free_port()
    clock_hz = args.clock_hz
    base_url = "http://127.0.0.1:{}".format(local_port)

    sim = None
    local = None
    collector = None
    failures = []

    def check(condition, label, detail=None):
        if condition:
            print("OK: {}".format(label))
        else:
            failures.append(label)
            print("FALLO: {}".format(label))
            if detail is not None:
                print("       {}".format(detail))

    try:
        sim = subprocess.Popen(
            [sim_path, "--a-port", str(a_port), "--b-port", str(b_port),
             "--clock-hz", str(clock_hz), "--trace"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
            bufsize=1,
        )
        collector = LineCollector(sim.stdout)
        check(wait_port(a_port, 5), "nodo A simulado disponible")
        check(wait_port(b_port, 5), "nodo B simulado disponible")

        local = subprocess.Popen(
            [sys.executable, LOCAL_SERVER,
             "--host", "127.0.0.1", "--port", str(local_port),
             "--beagle-host", "127.0.0.1", "--beagle-port", str(a_port)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        ping = wait_http_ok(base_url + "/api/ping", 5)
        check(ping is not None and ping.get("response") == "PONG",
              "gateway de la PC conectado al nodo A", ping)

        b_events = NodeEvents(b_port)

        status, data = http_json("POST", base_url + "/api/message",
                                 {"user": "ana", "text": "hola desde HTML"})
        check(status == 200 and data.get("response", "").startswith("OK SEND"),
              "HTML envia el mensaje a A", data)
        message = b_events.wait_message("ana", "hola desde HTML", 6)
        check(message is not None, "A -> B: mensaje en la terminal de B")
        if message is not None:
            print("       [B] {}: {}".format(message["user"], message["text"]))

        response = node_command(b_port, "SEND bob hola desde B")
        check(bool(response) and response[0].startswith("OK SEND"),
              "terminal de B envia el mensaje a A", response)
        inbox_since, message = wait_inbox_message(
            base_url, 0, "bob", "hola desde B", 6)
        check(message is not None, "B -> A -> HTML: mensaje en el inbox")
        check(message is not None and message.get("source") == "B",
              "el inbox identifica el origen B", message)

        status, data = http_json("POST", base_url + "/api/message",
                                 {"user": "ricardo", "text": "mañana"})
        check(status == 200, "HTML envia texto Latin-1", data)
        message = b_events.wait_message("ricardo", "mañana", 6)
        check(message is not None, "A -> B conserva acentos Latin-1")
        if message is not None:
            print("       [B] {}: {}".format(message["user"], message["text"]))

        response = node_command(b_port, "SEND bob canción")
        check(bool(response) and response[0].startswith("OK SEND"),
              "B envia texto Latin-1", response)
        inbox_since, message = wait_inbox_message(
            base_url, inbox_since, "bob", "canción", 6)
        check(message is not None, "B -> A -> HTML conserva acentos Latin-1")

        start = time.monotonic()
        status, data = http_json("POST", base_url + "/api/message",
                                 {"user": "ana", "text": "tiempo"})
        measured = b_events.wait_message("ana", "tiempo", 6)
        elapsed = time.monotonic() - start
        expected = (len("ana") + len("tiempo") + 2) * 44 * 2 / float(clock_hz)
        check(status == 200 and measured is not None,
              "medicion de tiempo con dos pulsos por bit")
        check(elapsed >= expected * 0.8,
              "el tiempo medido respeta los dos pulsos por bit",
              "medido {:.3f}s, esperado {:.3f}s".format(elapsed, expected))
        print("       tiempo medido {:.3f}s, esperado {:.3f}s a {} Hz".format(
            elapsed, expected, clock_hz))

        mark = collector.mark()
        corrupt = frame_bits(0, 3, ord("x"), corrupt=True)
        response = node_command(b_port, "BITS " + corrupt)
        check(bool(response) and response[0] == "OK BITS 44",
              "B inyecta un frame con CRC corrupto", response)
        line = collector.wait_for("status=CRC invalido", 5, mark)
        check(line is not None, "el nodo A registra el CRC invalido", line)
        ping = wait_http_ok(base_url + "/api/ping", 3)
        check(ping is not None and ping.get("response") == "PONG",
              "el nodo sigue vivo despues del CRC invalido")

        response = node_command(b_port, "SEND bob despues de crc")
        inbox_since, message = wait_inbox_message(
            base_url, inbox_since, "bob", "despues de crc", 6)
        check(message is not None, "el nodo sigue entregando mensajes despues del CRC")

        incomplete = frame_bits(0, 4, ord("y"))[:10]
        response = node_command(b_port, "BITS " + incomplete)
        check(bool(response) and response[0] == "OK BITS 10",
              "B inyecta una trama incompleta", response)
        time.sleep(0.4)
        status, data = http_json("GET", "{}/api/inbox?since={}".format(
            base_url, inbox_since))
        check(status == 200 and not data.get("messages"),
              "la trama incompleta no genera un mensaje", data)
        inbox_since = data.get("next", inbox_since)
        ping = wait_http_ok(base_url + "/api/ping", 3)
        check(ping is not None and ping.get("response") == "PONG",
              "el nodo sigue vivo despues de la trama incompleta")

        response = node_command(b_port, "SEND bob despues de incompleta")
        inbox_since, message = wait_inbox_message(
            base_url, inbox_since, "bob", "despues de incompleta", 6)
        check(message is not None,
              "el nodo sigue entregando mensajes despues de la trama incompleta")

    except Exception as exc:
        failures.append("excepcion")
        print("FALLO: {}".format(exc))
    finally:
        terminate(local)
        terminate(sim)

    if failures:
        print("\n{} pruebas fallaron".format(len(failures)))
        return 1
    print("\nOK: prueba integral de la fase 5")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
