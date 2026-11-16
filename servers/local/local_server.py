#!/usr/bin/env python3
"""Servidor local de la PC.

HTTP minimo en 127.0.0.1:8080.
  GET  /api/ping                       consulta la API del nodo de la Beagle.
  GET  /api/inbox?since=N              mensajes recibidos por el nodo de A.
  POST /api/message                    envia un mensaje con usuario al nodo.
  POST /api/bits                       envia una secuencia de bits al canal de luz.
  POST /api/frames                     inicia el emisor C de tramas.
  GET  /api/frames?job=N&since=M       consulta el progreso del emisor.
"""

import argparse
import json
import os
import socket
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn
from urllib.parse import parse_qs, urlsplit

MAX_BITS = 128
MAX_BODY = 4096
MAX_TEXT = 128
MAX_USER = 63
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
INDEX_PATH = os.path.join(BASE_DIR, "index.html")
TRANSLATOR_PATH = os.path.join(BASE_DIR, "translator.js")
DEFAULT_EMITTER = os.path.normpath(os.path.join(BASE_DIR, "..", "..", "c", "phy_light_tx"))
DEFAULT_BEAGLE_PORT = 7000

JOBS = {}
JOBS_LOCK = threading.Lock()
JOB_COUNTER = 0


def valid_bits(text):
    if not text or len(text) > MAX_BITS:
        return False
    for char in text:
        if char != "0" and char != "1":
            return False
    return True


class NodeClient:
    def __init__(self, host, port, timeout):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.stream = None
        self.lock = threading.Lock()

    def close(self):
        if self.stream is not None:
            try:
                self.stream.close()
            except OSError:
                pass
            self.stream = None
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def connect(self):
        sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        sock.settimeout(self.timeout)
        self.sock = sock
        self.stream = sock.makefile("r", encoding="utf-8", errors="replace")

    def command(self, command, multi=False):
        with self.lock:
            try:
                if self.sock is None:
                    self.connect()
                self.sock.sendall((command + "\n").encode("utf-8"))
                if multi:
                    lines = []
                    while True:
                        raw = self.stream.readline()
                        if not raw:
                            raise OSError("el nodo cerro la conexion")
                        line = raw.rstrip("\n")
                        lines.append(line)
                        if line.startswith("NEXT "):
                            return lines
                raw = self.stream.readline()
                if not raw:
                    raise OSError("el nodo cerro la conexion")
                return [raw.rstrip("\n")]
            except OSError:
                self.close()
                raise


def ping_beagle(client):
    try:
        lines = client.command("PING")
    except OSError as exc:
        return 502, {"beagle": "unreachable", "error": str(exc)}
    response = lines[0] if lines else ""
    if response == "PONG":
        return 200, {"beagle": "ok", "response": response}
    return 502, {"beagle": "unexpected", "response": response}


def send_bits(client, bits):
    try:
        lines = client.command("BITS " + bits)
    except OSError as exc:
        return 502, {"beagle": "unreachable", "error": str(exc)}
    response = lines[0] if lines else ""
    if response == "OK BITS {}".format(len(bits)):
        return 200, {"beagle": "ok", "bits": bits, "response": response}
    return 502, {"beagle": "error", "bits": bits, "response": response}


def send_message(client, user, text):
    try:
        lines = client.command("SEND {} {}".format(user, text))
    except OSError as exc:
        return 502, {"beagle": "unreachable", "error": str(exc)}
    response = lines[0] if lines else ""
    if response.startswith("OK SEND"):
        return 200, {"beagle": "ok", "response": response}
    return 502, {"beagle": "error", "response": response}


def poll_inbox(client, since):
    try:
        lines = client.command("POLL {}".format(since), multi=True)
    except OSError as exc:
        return 502, {"beagle": "unreachable", "error": str(exc)}

    messages = []
    next_index = None
    for line in lines:
        if line.startswith("NEXT "):
            try:
                next_index = int(line[5:].strip())
            except ValueError:
                return 502, {"beagle": "error", "error": "NEXT invalido"}
        elif line.startswith("MESSAGE "):
            parts = line.split(" ", 3)
            if len(parts) == 4:
                try:
                    event_id = int(parts[1])
                except ValueError:
                    event_id = parts[1]
                messages.append({
                    "id": event_id,
                    "user": parts[2],
                    "text": parts[3],
                    "source": "B",
                })
    if next_index is None:
        return 502, {"beagle": "error", "error": "respuesta de POLL invalida"}
    return 200, {"beagle": "ok", "messages": messages, "next": next_index}


def valid_user(user):
    if not isinstance(user, str) or not user or len(user) > MAX_USER:
        return False
    for char in user:
        if char.isspace() or ord(char) < 32 or ord(char) == 127:
            return False
    return True


def valid_text(text):
    if not isinstance(text, str) or not text or len(text) > MAX_TEXT:
        return False
    for char in text:
        if ord(char) < 32 or ord(char) == 127:
            return False
    return True


def create_job():
    global JOB_COUNTER

    with JOBS_LOCK:
        JOB_COUNTER += 1
        job_id = JOB_COUNTER
        JOBS[job_id] = {
            "lines": [],
            "done": False,
            "ok": None,
        }
        return job_id


def append_job_line(job_id, line):
    with JOBS_LOCK:
        job = JOBS.get(job_id)
        if job is not None:
            job["lines"].append(line)


def finish_job(job_id, ok):
    with JOBS_LOCK:
        job = JOBS.get(job_id)
        if job is not None:
            job["done"] = True
            job["ok"] = ok


def run_emitter_job(job_id, command):
    try:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
    except OSError as exc:
        append_job_line(job_id, "Error al ejecutar el emisor: {}".format(exc))
        finish_job(job_id, False)
        return

    try:
        for line in process.stdout:
            append_job_line(job_id, line.rstrip("\n"))
    finally:
        returncode = process.wait()
        finish_job(job_id, returncode == 0)


def start_frames_job(text, server):
    emitter = server.emitter_path
    if not os.path.isfile(emitter) or not os.access(emitter, os.X_OK):
        return None, "emisor no encontrado: {}".format(emitter)

    command = [
        emitter,
        "--host", server.beagle_host,
        "--port", str(server.beagle_port),
        "--encoding", server.emitter_encoding,
        "--message", text,
    ]
    job_id = create_job()
    thread = threading.Thread(target=run_emitter_job, args=(job_id, command))
    thread.daemon = True
    thread.start()
    return job_id, None


def frames_status(job_id, since):
    with JOBS_LOCK:
        job = JOBS.get(job_id)
        if job is None:
            return None
        lines = job["lines"]
        next_index = len(lines)
        if since < 0:
            since = 0
        if since > next_index:
            since = next_index
        return {
            "job": job_id,
            "lines": lines[since:],
            "next": next_index,
            "done": job["done"],
            "ok": job["ok"],
        }


class Handler(BaseHTTPRequestHandler):
    server_version = "LocalServer/0.5"

    def do_GET(self):
        parsed = urlsplit(self.path)
        path = parsed.path

        if path == "/" or path == "/index.html":
            self.send_file(INDEX_PATH, "text/html; charset=utf-8")
        elif path == "/translator.js":
            self.send_file(TRANSLATOR_PATH, "application/javascript; charset=utf-8")
        elif path == "/api/ping":
            status, payload = ping_beagle(self.server.node_client)
            self.send_json(status, payload)
        elif path == "/api/inbox":
            self.handle_inbox(parsed.query)
        elif path == "/api/frames":
            self.handle_frames_status(parsed.query)
        else:
            self.send_json(404, {"error": "not found"})

    def do_POST(self):
        path = urlsplit(self.path).path

        if path == "/api/bits":
            self.handle_bits_post()
        elif path == "/api/message":
            self.handle_message_post()
        elif path == "/api/frames":
            self.handle_frames_post()
        else:
            self.send_json(404, {"error": "not found"})

    def handle_bits_post(self):
        payload = self.read_json()
        if payload is None:
            return

        bits = payload.get("bits")
        if not isinstance(bits, str) or not valid_bits(bits):
            self.send_json(400, {"error": "bits debe ser una cadena de 0 y 1"})
            return

        status, result = send_bits(self.server.node_client, bits)
        self.send_json(status, result)

    def handle_message_post(self):
        payload = self.read_json()
        if payload is None:
            return

        user = payload.get("user")
        text = payload.get("text")
        if not valid_user(user):
            self.send_json(400, {"error": "user invalido; sin espacios y hasta {} caracteres".format(MAX_USER)})
            return
        if not valid_text(text):
            self.send_json(400, {"error": "text invalido; no vacio y hasta {} caracteres".format(MAX_TEXT)})
            return

        status, result = send_message(self.server.node_client, user, text)
        self.send_json(status, result)

    def handle_inbox(self, query):
        params = parse_qs(query)
        try:
            since = int(params.get("since", ["0"])[0])
        except ValueError:
            self.send_json(400, {"error": "since debe ser un entero"})
            return
        if since < 0:
            self.send_json(400, {"error": "since no puede ser negativo"})
            return

        status, result = poll_inbox(self.server.node_client, since)
        self.send_json(status, result)

    def handle_frames_post(self):
        payload = self.read_json()
        if payload is None:
            return

        text = payload.get("text")
        if not isinstance(text, str) or not text:
            self.send_json(400, {"error": "text no puede estar vacio"})
            return
        if len(text) > MAX_TEXT:
            self.send_json(400, {"error": "text supera los {} caracteres".format(MAX_TEXT)})
            return

        job_id, error = start_frames_job(text, self.server)
        if error is not None:
            self.send_json(500, {"error": error})
            return
        self.send_json(202, {"job": job_id})

    def handle_frames_status(self, query):
        params = parse_qs(query)
        try:
            job_id = int(params.get("job", [""])[0])
            since = int(params.get("since", ["0"])[0])
        except ValueError:
            self.send_json(400, {"error": "job y since deben ser enteros"})
            return

        result = frames_status(job_id, since)
        if result is None:
            self.send_json(404, {"error": "trabajo no encontrado"})
            return
        self.send_json(200, result)

    def read_json(self):
        length = self.headers.get("Content-Length")
        try:
            length = int(length)
        except (TypeError, ValueError):
            self.send_json(400, {"error": "Content-Length invalido"})
            return None
        if length <= 0 or length > MAX_BODY:
            self.send_json(400, {"error": "cuerpo invalido"})
            return None

        body = self.rfile.read(length)
        try:
            payload = json.loads(body.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            self.send_json(400, {"error": "JSON invalido"})
            return None
        if not isinstance(payload, dict):
            self.send_json(400, {"error": "se esperaba un objeto JSON"})
            return None
        return payload

    def send_file(self, path, content_type):
        try:
            with open(path, "rb") as handle:
                body = handle.read()
        except OSError:
            self.send_json(500, {"error": "archivo no disponible"})
            return
        try:
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def send_json(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        try:
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


class ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser(description="Servidor local de la PC")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--beagle-host", default="192.168.7.2")
    parser.add_argument("--beagle-port", type=int, default=DEFAULT_BEAGLE_PORT)
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="timeout de conexion y de comandos al nodo")
    parser.add_argument("--emitter", default=DEFAULT_EMITTER,
                        help="ruta al binario phy_light_tx")
    parser.add_argument("--emitter-encoding", default="latin-1",
                        help="codificacion usada por el emisor C")
    args = parser.parse_args()

    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    httpd.beagle_host = args.beagle_host
    httpd.beagle_port = args.beagle_port
    httpd.node_client = NodeClient(args.beagle_host, args.beagle_port, args.timeout)
    httpd.emitter_path = args.emitter
    httpd.emitter_encoding = args.emitter_encoding
    print("[local] escuchando en http://{}:{}/".format(args.host, args.port), flush=True)
    print("[local] Beagle en {}:{}".format(args.beagle_host, args.beagle_port), flush=True)
    print("[local] emisor {} ({})".format(
        args.emitter,
        "ok" if os.access(args.emitter, os.X_OK) else "no encontrado"), flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[local] detenido", flush=True)
    finally:
        httpd.node_client.close()
        httpd.server_close()


if __name__ == "__main__":
    main()
