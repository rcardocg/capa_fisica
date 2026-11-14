#!/usr/bin/env python3
import argparse
import json
import os
import select
import socket
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from capa_fisica.protocol.crc import available_algorithms, normalize_algorithm
from capa_fisica.protocol.frame import (
    CONTROL_ACK,
    CONTROL_BITS,
    CONTROL_DATA,
    CONTROL_NACK,
    CONTROL_NAMES,
    CRC_BITS,
    DATA_BITS,
    FRAME_BITS,
    SEQ_BITS,
    SFD_BITS,
    SFD_PATTERN,
    SYN_BITS,
    SYN_PATTERN,
    Frame,
    FrameError,
    bits_to_int,
)
from capa_fisica.protocol.presentation import (
    IncrementalDecoder,
    available_encodings,
    message_payload,
    normalize_encoding,
)


class TraceLogger(object):
    def __init__(self, label, enabled=True):
        self.label = label
        self.enabled = enabled

    def __call__(self, message):
        if self.enabled:
            sys.stderr.write("[%s] %s\n" % (self.label, message))
            sys.stderr.flush()


class JsonSocket(object):
    def __init__(self, sock):
        self.sock = sock
        self.buffer = b""

    def send(self, record):
        payload = json.dumps(record, separators=(",", ":")).encode("utf-8")
        self.sock.sendall(payload + b"\n")

    def recv(self, timeout=None):
        deadline = None
        if timeout is not None:
            deadline = time.monotonic() + timeout
        while True:
            while b"\n" not in self.buffer:
                if deadline is None:
                    ready = True
                else:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        return None
                    try:
                        ready = bool(select.select([self.sock], [], [], remaining)[0])
                    except OSError:
                        return None
                    if not ready:
                        return None
                try:
                    chunk = self.sock.recv(4096)
                except OSError:
                    return None
                if not chunk:
                    return None
                self.buffer += chunk
            line, self.buffer = self.buffer.split(b"\n", 1)
            if not line.strip():
                continue
            try:
                return json.loads(line.decode("utf-8"))
            except (UnicodeDecodeError, ValueError):
                return {"type": "error", "error": "invalid json"}


def add_common_arguments(subparser):
    subparser.add_argument(
        "--encoding",
        default="utf-8",
        choices=available_encodings(),
        help="Text encoding used by the presentation layer",
    )
    subparser.add_argument(
        "--crc",
        default="smbus",
        choices=available_algorithms(),
        help="CRC-8 algorithm",
    )
    subparser.add_argument(
        "--lanes",
        type=int,
        default=3,
        help="Data lanes (physical channels minus the clock channel)",
    )
    subparser.add_argument(
        "--seq-start",
        type=int,
        default=0,
        help="Initial 4-bit sequence number",
    )
    subparser.add_argument(
        "--quiet",
        action="store_true",
        help="Do not print the protocol trace to stderr",
    )


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Terminal-to-terminal test tool for the PHY protocol"
    )
    subparsers = parser.add_subparsers(dest="mode")

    tx_parser = subparsers.add_parser("tx", help="Encode stdin lines into frames")
    add_common_arguments(tx_parser)
    tx_parser.add_argument(
        "--corrupt-frame",
        type=int,
        default=-1,
        help="Zero-based frame index to corrupt",
    )
    tx_parser.add_argument(
        "--corrupt-bit",
        type=int,
        default=FRAME_BITS - 1,
        help="Wire bit index to flip when corrupting a frame",
    )

    rx_parser = subparsers.add_parser("rx", help="Decode frames from stdin")
    add_common_arguments(rx_parser)

    loop_parser = subparsers.add_parser(
        "loopback", help="Encode and decode without a pipe"
    )
    add_common_arguments(loop_parser)
    loop_parser.add_argument("text", nargs="*", help="Text to process")

    connect_parser = subparsers.add_parser(
        "connect", help="Interactive emitter connected to a receiver"
    )
    add_common_arguments(connect_parser)
    connect_parser.add_argument("--host", default="127.0.0.1")
    connect_parser.add_argument("--port", type=int, default=5000)
    connect_parser.add_argument("--timeout", type=float, default=1.0)
    connect_parser.add_argument("--retries", type=int, default=3)
    connect_parser.add_argument(
        "--corrupt-frame",
        type=int,
        default=-1,
        help="Zero-based frame index to corrupt",
    )
    connect_parser.add_argument(
        "--corrupt-bit",
        type=int,
        default=FRAME_BITS - 1,
        help="Wire bit index to flip when corrupting a frame",
    )

    serve_parser = subparsers.add_parser(
        "serve", help="Interactive receiver waiting for an emitter"
    )
    add_common_arguments(serve_parser)
    serve_parser.add_argument("--host", default="127.0.0.1")
    serve_parser.add_argument("--port", type=int, default=5000)

    return parser, parser.parse_args(argv)


def frame_record(frame, lanes, encoding, corrupted):
    record = frame.to_dict()
    record["type"] = "frame"
    record["encoding"] = normalize_encoding(encoding)
    record["lanes"] = lanes
    record["corrupted"] = bool(corrupted)
    record["frame_bits"] = frame.to_bits()
    record["wire_bits"] = frame.to_wire_bits(lanes)
    return record


def bits_as_text(bits):
    return "".join(str(bit) for bit in bits)


def wire_length(lanes):
    return FRAME_BITS + ((-FRAME_BITS) % lanes)


def trace_frame_fields(trace, direction, wire, crc_algorithm, corrupted=False):
    if len(wire) < FRAME_BITS:
        trace(
            "%s trama incompleta: %d bits (mínimo %d)"
            % (direction, len(wire), FRAME_BITS)
        )
        return
    syn = bits_to_int(wire[:SYN_BITS])
    sfd = bits_to_int(wire[SYN_BITS:SYN_BITS + SFD_BITS])
    control_position = SYN_BITS + SFD_BITS
    control = bits_to_int(
        wire[control_position:control_position + CONTROL_BITS]
    )
    seq_position = control_position + CONTROL_BITS
    seq = bits_to_int(wire[seq_position:seq_position + SEQ_BITS])
    data_position = seq_position + SEQ_BITS
    data = bits_to_int(wire[data_position:data_position + DATA_BITS])
    crc_position = data_position + DATA_BITS
    received_crc = bits_to_int(
        wire[crc_position:crc_position + CRC_BITS]
    )
    expected_crc = Frame(
        control, data, seq, crc_algorithm, received_crc
    ).compute_crc()
    trace(
        "%s SYN=0x%04X %s"
        % (direction, syn, "OK" if syn == SYN_PATTERN else "FAIL")
    )
    trace(
        "%s SFD=0x%X %s"
        % (direction, sfd, "OK" if sfd == SFD_PATTERN else "FAIL")
    )
    trace(
        "%s CONTROL=0x%X (%s) SEQ=%d"
        % (
            direction,
            control,
            CONTROL_NAMES.get(control, "RESERVED_%X" % control),
            seq,
        )
    )
    trace(
        "%s DATA=0x%02X %r"
        % (direction, data, bytes(bytearray([data])))
    )
    trace(
        "%s CRC=0x%02X (%s) esperado=0x%02X %s"
        % (
            direction,
            received_crc,
            crc_algorithm,
            expected_crc,
            "OK" if received_crc == expected_crc else "FAIL",
        )
    )
    trace("%s WIRE=%s" % (direction, bits_as_text(wire)))
    if corrupted:
        trace("%s incluye corrupción inducida" % direction)


def build_wire_and_record(frame, lanes, encoding, corrupt_bit=None):
    wire = frame.to_wire_bits(lanes)
    corrupted = False
    if corrupt_bit is not None:
        if corrupt_bit < 0 or corrupt_bit >= len(wire):
            raise ValueError("corrupt-bit is outside the wire frame")
        wire[corrupt_bit] ^= 1
        corrupted = True
    record = frame_record(frame, lanes, encoding, corrupted)
    record["wire_bits"] = wire
    return wire, record, corrupted


def send_frame(json_socket, frame, lanes, encoding, crc_algorithm, trace,
               direction="TX", corrupt_bit=None):
    wire, record, corrupted = build_wire_and_record(
        frame, lanes, encoding, corrupt_bit
    )
    trace_frame_fields(trace, direction, wire, crc_algorithm, corrupted)
    json_socket.send(record)
    return wire, corrupted


def send_control(json_socket, control, seq, args, trace):
    frame = Frame(control, 0, seq, args.crc)
    trace(
        "ENVIANDO %s SEQ=%d"
        % (frame.control_name(), frame.seq)
    )
    send_frame(
        json_socket,
        frame,
        args.lanes,
        args.encoding,
        args.crc,
        trace,
        "TX",
        None,
    )
    return frame


def wait_for_control(json_socket, seq, args, trace):
    trace(
        "esperando ACK/NACK para SEQ=%d (timeout %.2f s)"
        % (seq, args.timeout)
    )
    record = json_socket.recv(args.timeout)
    if record is None:
        trace("TIMEOUT: no llegó ACK/NACK")
        return None, "TIMEOUT"
    wire = record.get("wire_bits")
    if not isinstance(wire, list):
        trace("RESPUESTA inválida: falta wire_bits")
        return None, "BAD_RECORD"
    trace_frame_fields(trace, "RX", wire, args.crc, False)
    try:
        frame = Frame.from_wire_bits(wire, args.lanes, args.crc)
    except (FrameError, TypeError, ValueError) as exc:
        trace("RESPUESTA inválida: %s" % exc)
        return None, "BAD_FRAME"
    if frame.is_ack():
        if frame.seq == seq:
            return frame, "ACK"
        trace(
            "ACK con SEQ incorrecto: recibido=%d esperado=%d"
            % (frame.seq, seq)
        )
        return frame, "ACK_WRONG_SEQ"
    if frame.is_nack():
        trace("NACK recibido para SEQ=%d" % frame.seq)
        return frame, "NACK"
    trace("CONTROL inesperado: %s" % frame.control_name())
    return frame, "UNEXPECTED"


def send_data_with_ack(json_socket, seq, value, args, trace, corrupt_bit):
    attempts = 0
    while True:
        frame = Frame(CONTROL_DATA, value, seq, args.crc)
        active_corrupt = corrupt_bit if attempts == 0 else None
        send_frame(
            json_socket,
            frame,
            args.lanes,
            args.encoding,
            args.crc,
            trace,
            "TX",
            active_corrupt,
        )
        response, status = wait_for_control(json_socket, seq, args, trace)
        if status == "ACK":
            trace("ACK SEQ=%d OK" % seq)
            return True, attempts
        attempts += 1
        if attempts > args.retries:
            trace("ERROR: máximo de reintentos para SEQ=%d" % seq)
            return False, attempts
        trace(
            "REINTENTO %d/%d para SEQ=%d"
            % (attempts, args.retries, seq)
        )


def command_connect(args):
    trace = TraceLogger("EMISOR", not args.quiet)
    trace("conectando a %s:%d" % (args.host, args.port))
    try:
        sock = socket.create_connection(
            (args.host, args.port), timeout=args.timeout
        )
    except OSError as exc:
        trace("ERROR de conexión: %s" % exc)
        return 1
    json_socket = JsonSocket(sock)
    trace("conectado; escribí un mensaje y presioná Enter")
    sequence = args.seq_start % 16
    frame_index = 0
    try:
        while True:
            sys.stderr.write("[EMISOR] mensaje> ")
            sys.stderr.flush()
            raw_line = sys.stdin.readline()
            if not raw_line:
                break
            line = raw_line.rstrip("\n")
            if line.endswith("\r"):
                line = line[:-1]
            try:
                payload = message_payload(line, args.encoding)
            except (TypeError, ValueError) as exc:
                trace("ERROR de codificación: %s" % exc)
                continue
            trace(
                "mensaje=%r; bytes=%d (incluye \\n)"
                % (line, len(payload))
            )
            aborted = False
            for index, value in enumerate(bytearray(payload)):
                trace(
                    "BYTE %d/%d 0x%02X %r"
                    % (
                        index + 1,
                        len(payload),
                        value,
                        bytes(bytearray([value])),
                    )
                )
                corrupt_bit = None
                if frame_index == args.corrupt_frame:
                    corrupt_bit = args.corrupt_bit
                ok, attempts = send_data_with_ack(
                    json_socket,
                    sequence,
                    value,
                    args,
                    trace,
                    corrupt_bit,
                )
                if not ok:
                    trace("mensaje abortado en byte 0x%02X" % value)
                    aborted = True
                    break
                frame_index += 1
                sequence = (sequence + 1) % 16
            if aborted:
                continue
            trace("mensaje enviado y confirmado")
    except KeyboardInterrupt:
        trace("interrumpido por el usuario")
    finally:
        try:
            sock.close()
        except OSError:
            pass
        trace("conexión cerrada")
    return 0


def handle_receiver_connection(connection, args, trace):
    json_socket = JsonSocket(connection)
    decoder = IncrementalDecoder(args.encoding)
    expected_seq = 0
    last_delivered = None
    message_text = ""
    while True:
        record = json_socket.recv()
        if record is None:
            break
        wire = record.get("wire_bits")
        if not isinstance(wire, list):
            trace("RX ERROR: registro sin wire_bits")
            continue
        trace("RX trama recibida: %d bits" % len(wire))
        trace_frame_fields(trace, "RX", wire, args.crc, False)
        if len(wire) < FRAME_BITS:
            trace("FALLO: trama incompleta -> NACK")
            send_control(json_socket, CONTROL_NACK, 0, args, trace)
            continue
        seq = bits_to_int(wire[24:28])
        syn = bits_to_int(wire[:SYN_BITS])
        if syn != SYN_PATTERN:
            trace(
                "FALLO EN SYN: recibido=0x%04X esperado=0x%04X -> NACK"
                % (syn, SYN_PATTERN)
            )
            send_control(json_socket, CONTROL_NACK, seq, args, trace)
            continue
        sfd = bits_to_int(wire[SYN_BITS:SYN_BITS + SFD_BITS])
        if sfd != SFD_PATTERN:
            trace(
                "FALLO EN SFD: recibido=0x%X esperado=0x%X -> NACK"
                % (sfd, SFD_PATTERN)
            )
            send_control(json_socket, CONTROL_NACK, seq, args, trace)
            continue
        try:
            frame = Frame.from_wire_bits(wire, args.lanes, args.crc)
        except (FrameError, TypeError, ValueError) as exc:
            trace("FALLO EN CRC/FRAME: %s -> NACK" % exc)
            send_control(json_socket, CONTROL_NACK, seq, args, trace)
            continue
        trace("CRC OK")
        if frame.control != CONTROL_DATA:
            trace(
                "CONTROL %s ignorado en el receptor"
                % frame.control_name()
            )
            continue
        if frame.seq == expected_seq:
            try:
                text = decoder.feed(frame.data)
            except UnicodeDecodeError as exc:
                trace("ERROR de decodificación: %s" % exc)
                text = ""
            if text:
                sys.stdout.write(text)
                sys.stdout.flush()
                trace("TEXTO recibido=%r" % text)
                message_text += text
                while "\n" in message_text:
                    complete, message_text = message_text.split("\n", 1)
                    trace(
                        "MENSAJE COMPLETO (%d caracteres): %r"
                        % (len(complete), complete)
                    )
            last_delivered = frame.seq
            expected_seq = (expected_seq + 1) % 16
            send_control(json_socket, CONTROL_ACK, frame.seq, args, trace)
            trace("ACK SEQ=%d enviado" % frame.seq)
        elif last_delivered is not None and frame.seq == last_delivered:
            trace("DUPLICADO SEQ=%d -> ACK sin entregar" % frame.seq)
            send_control(json_socket, CONTROL_ACK, frame.seq, args, trace)
            trace("ACK SEQ=%d enviado (duplicado)" % frame.seq)
        else:
            trace(
                "SEQ fuera de orden: recibido=%d esperado=%d -> NACK"
                % (frame.seq, expected_seq)
            )
            send_control(json_socket, CONTROL_NACK, frame.seq, args, trace)
    if message_text:
        trace(
            "MENSAJE INCOMPLETO (%d caracteres): %r"
            % (len(message_text), message_text)
        )
    try:
        tail = decoder.finish()
    except UnicodeDecodeError as exc:
        trace("ERROR de decodificación al cerrar: %s" % exc)
        tail = ""
    if tail:
        sys.stdout.write(tail)
        sys.stdout.flush()
    trace("conexión terminada")


def command_serve(args):
    trace = TraceLogger("RECEPTOR", not args.quiet)
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server.bind((args.host, args.port))
        server.listen(1)
    except OSError as exc:
        trace(
            "ERROR al escuchar en %s:%d: %s"
            % (args.host, args.port, exc)
        )
        server.close()
        return 1
    trace("escuchando en %s:%d" % (args.host, args.port))
    try:
        while True:
            try:
                connection, address = server.accept()
            except KeyboardInterrupt:
                trace("interrumpido por el usuario")
                break
            except OSError as exc:
                trace("ERROR al aceptar conexión: %s" % exc)
                break
            trace(
                "conexión aceptada desde %s:%d"
                % (address[0], address[1])
            )
            handle_receiver_connection(connection, args, trace)
            try:
                connection.close()
            except OSError:
                pass
            trace("conexión cerrada; esperando otra")
    finally:
        server.close()
    return 0


def command_tx(args):
    sequence = args.seq_start % 16
    frame_index = 0
    for raw_line in sys.stdin:
        line = raw_line.rstrip("\n")
        if line.endswith("\r"):
            line = line[:-1]
        try:
            payload = message_payload(line, args.encoding)
        except (TypeError, ValueError) as exc:
            sys.stderr.write("TX encode error: %s\n" % exc)
            continue
        for value in bytearray(payload):
            frame = Frame(CONTROL_DATA, value, sequence, args.crc)
            wire, record, corrupted = build_wire_and_record(
                frame,
                args.lanes,
                args.encoding,
                args.corrupt_bit if frame_index == args.corrupt_frame else None,
            )
            sys.stdout.write(json.dumps(record, separators=(",", ":")) + "\n")
            sys.stdout.flush()
            if not args.quiet:
                sys.stderr.write(
                    "TX frame=%d seq=%d data=0x%02X crc=0x%02X%s wire=%s\n"
                    % (
                        frame_index,
                        frame.seq,
                        frame.data,
                        frame.crc,
                        " CORRUPTED" if corrupted else "",
                        bits_as_text(wire),
                    )
                )
            sequence = (sequence + 1) % 16
            frame_index += 1
    return 0


def command_rx(args):
    decoder = IncrementalDecoder(args.encoding)
    expected_crc = normalize_algorithm(args.crc)
    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue
        try:
            record = json.loads(line)
        except ValueError as exc:
            sys.stderr.write("RX JSON error: %s\n" % exc)
            continue
        wire = record.get("wire_bits")
        if not isinstance(wire, list):
            sys.stderr.write("RX JSON error: wire_bits is missing\n")
            continue
        recorded_crc = record.get("crc_algorithm")
        if recorded_crc is not None:
            try:
                if normalize_algorithm(recorded_crc) != expected_crc:
                    sys.stderr.write(
                        "RX warning: frame CRC %s, receiver CRC %s\n"
                        % (recorded_crc, expected_crc)
                    )
            except ValueError:
                sys.stderr.write("RX warning: unknown frame CRC algorithm\n")
        try:
            frame = Frame.from_wire_bits(wire, args.lanes, expected_crc)
        except (FrameError, TypeError, ValueError) as exc:
            sys.stderr.write("RX FRAME ERROR: %s\n" % exc)
            continue
        if not frame.is_data():
            if not args.quiet:
                sys.stderr.write(
                    "RX control=%s seq=%d data=0x%02X crc=0x%02X\n"
                    % (frame.control_name(), frame.seq, frame.data, frame.crc)
                )
            continue
        try:
            text = decoder.feed(frame.data)
        except UnicodeDecodeError as exc:
            sys.stderr.write("RX DECODE ERROR: %s\n" % exc)
            continue
        if text:
            sys.stdout.write(text)
            sys.stdout.flush()
        if not args.quiet:
            sys.stderr.write(
                "RX seq=%d data=0x%02X crc=0x%02X OK\n"
                % (frame.seq, frame.data, frame.crc)
            )
    try:
        tail = decoder.finish()
    except UnicodeDecodeError as exc:
        sys.stderr.write("RX DECODE ERROR at end: %s\n" % exc)
        tail = ""
    if tail:
        sys.stdout.write(tail)
        sys.stdout.flush()
    return 0


def command_loopback(args):
    if args.text:
        lines = [" ".join(args.text)]
    else:
        lines = [line.rstrip("\n") for line in sys.stdin]
    sequence = args.seq_start % 16
    decoder = IncrementalDecoder(args.encoding)
    output = []
    frame_index = 0
    for line in lines:
        try:
            payload = message_payload(line, args.encoding)
        except (TypeError, ValueError) as exc:
            sys.stderr.write("Loopback encode error: %s\n" % exc)
            continue
        for value in bytearray(payload):
            frame = Frame(CONTROL_DATA, value, sequence, args.crc)
            wire = frame.to_wire_bits(args.lanes)
            rebuilt = Frame.from_wire_bits(wire, args.lanes, args.crc)
            if not args.quiet:
                sys.stderr.write(
                    "FRAME %d seq=%d data=0x%02X crc=0x%02X bits=%s\n"
                    % (
                        frame_index,
                        frame.seq,
                        frame.data,
                        frame.crc,
                        bits_as_text(wire),
                    )
                )
            output.append(decoder.feed(rebuilt.data))
            sequence = (sequence + 1) % 16
            frame_index += 1
    output.append(decoder.finish())
    sys.stdout.write("".join(output))
    sys.stdout.flush()
    return 0


def main(argv=None):
    parser, args = parse_arguments(argv)
    if args.mode is None:
        parser.print_help()
        return 2
    if args.lanes < 1 or args.lanes > 7:
        parser.error("--lanes must be between 1 and 7")
    try:
        args.crc = normalize_algorithm(args.crc)
        args.encoding = normalize_encoding(args.encoding)
    except ValueError as exc:
        parser.error(str(exc))
    if args.seq_start < 0:
        parser.error("--seq-start must be non-negative")
    if args.mode in ("tx", "connect"):
        maximum_bit = wire_length(args.lanes) - 1
        if args.corrupt_bit < 0 or args.corrupt_bit > maximum_bit:
            parser.error(
                "--corrupt-bit must be between 0 and %d" % maximum_bit
            )
    if args.mode in ("connect", "serve"):
        if args.port < 1 or args.port > 65535:
            parser.error("--port must be between 1 and 65535")
        if not args.host:
            parser.error("--host must not be empty")
    if args.mode == "connect":
        if args.timeout <= 0:
            parser.error("--timeout must be positive")
        if args.retries < 0:
            parser.error("--retries must be non-negative")
    if args.mode == "tx":
        return command_tx(args)
    if args.mode == "rx":
        return command_rx(args)
    if args.mode == "loopback":
        return command_loopback(args)
    if args.mode == "connect":
        return command_connect(args)
    if args.mode == "serve":
        return command_serve(args)
    parser.error("Unknown mode")
    return 2


if __name__ == "__main__":
    sys.exit(main())
