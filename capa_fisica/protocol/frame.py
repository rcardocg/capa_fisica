from .crc import crc8, normalize_algorithm

SYN_PATTERN = 0xAAAA
SYN_BITS = 16
SFD_PATTERN = 0xD
SFD_BITS = 4
CONTROL_BITS = 4
SEQ_BITS = 4
DATA_BITS = 8
CRC_BITS = 8
FRAME_BITS = SYN_BITS + SFD_BITS + CONTROL_BITS + SEQ_BITS + DATA_BITS + CRC_BITS

DEFAULT_CRC = "smbus"

CONTROL_DATA = 0x0
CONTROL_ACK = 0x1
CONTROL_NACK = 0x2
CONTROL_HELLO = 0x3
CONTROL_CONFIG = 0x4
CONTROL_STATUS = 0x5
CONTROL_ABORT = 0xF

CONTROL_NAMES = {
    CONTROL_DATA: "DATA",
    CONTROL_ACK: "ACK",
    CONTROL_NACK: "NACK",
    CONTROL_HELLO: "HELLO",
    CONTROL_CONFIG: "CONFIG",
    CONTROL_STATUS: "STATUS",
    CONTROL_ABORT: "ABORT",
}


class FrameError(ValueError):
    pass


def int_to_bits(value, width):
    if width < 1:
        raise ValueError("Bit width must be positive")
    if value < 0 or value >= (1 << width):
        raise ValueError("Value %s does not fit in %s bits" % (value, width))
    return [(value >> shift) & 1 for shift in range(width - 1, -1, -1)]


def bits_to_int(bits):
    value = 0
    for bit in bits:
        value = (value << 1) | (1 if bit else 0)
    return value


def _validate_nibble(value, field_name):
    if not isinstance(value, int):
        raise TypeError("%s must be an integer" % field_name)
    if value < 0 or value > 0xF:
        raise ValueError("%s must be between 0 and 15" % field_name)
    return value


def _validate_byte(value, field_name):
    if not isinstance(value, int):
        raise TypeError("%s must be an integer" % field_name)
    if value < 0 or value > 0xFF:
        raise ValueError("%s must be between 0 and 255" % field_name)
    return value


def _validate_bits(bits, expected):
    values = list(bits)
    if len(values) != expected:
        raise FrameError("Expected %s bits, got %s" % (expected, len(values)))
    for bit in values:
        if bit not in (0, 1, False, True):
            raise FrameError("Bit values must be 0 or 1")
    return [1 if bit else 0 for bit in values]


class Frame(object):
    def __init__(self, control, data, seq=0, crc_algorithm=DEFAULT_CRC, crc=None):
        self.control = _validate_nibble(control, "control")
        self.seq = _validate_nibble(seq, "seq")
        self.data = _validate_byte(data, "data")
        self.crc_algorithm = normalize_algorithm(crc_algorithm)
        if crc is None:
            self.crc = self.compute_crc()
        else:
            self.crc = _validate_byte(crc, "crc")

    def compute_crc(self):
        payload = bytearray()
        payload.append((self.control << 4) | self.seq)
        payload.append(self.data)
        return crc8(bytes(payload), self.crc_algorithm)

    def verify_crc(self):
        return self.crc == self.compute_crc()

    def control_name(self):
        return CONTROL_NAMES.get(self.control, "RESERVED_%X" % self.control)

    def is_data(self):
        return self.control == CONTROL_DATA

    def is_ack(self):
        return self.control == CONTROL_ACK

    def is_nack(self):
        return self.control == CONTROL_NACK

    def to_bits(self):
        bits = []
        bits.extend(int_to_bits(SYN_PATTERN, SYN_BITS))
        bits.extend(int_to_bits(SFD_PATTERN, SFD_BITS))
        bits.extend(int_to_bits(self.control, CONTROL_BITS))
        bits.extend(int_to_bits(self.seq, SEQ_BITS))
        bits.extend(int_to_bits(self.data, DATA_BITS))
        bits.extend(int_to_bits(self.crc, CRC_BITS))
        return bits

    def to_wire_bits(self, data_lanes):
        if not isinstance(data_lanes, int) or data_lanes < 1:
            raise ValueError("data_lanes must be a positive integer")
        bits = self.to_bits()
        padding = (-len(bits)) % data_lanes
        if padding:
            bits.extend([0] * padding)
        return bits

    def to_dict(self):
        return {
            "control": self.control,
            "control_name": self.control_name(),
            "seq": self.seq,
            "data": self.data,
            "crc": self.crc,
            "crc_algorithm": self.crc_algorithm,
        }

    @classmethod
    def from_bits(cls, bits, crc_algorithm=DEFAULT_CRC, verify=True):
        values = _validate_bits(bits, FRAME_BITS)
        position = 0
        syn = bits_to_int(values[position:position + SYN_BITS])
        position += SYN_BITS
        if syn != SYN_PATTERN:
            raise FrameError("Invalid SYN pattern: 0x%04X" % syn)
        sfd = bits_to_int(values[position:position + SFD_BITS])
        position += SFD_BITS
        if sfd != SFD_PATTERN:
            raise FrameError("Invalid SFD pattern: 0x%X" % sfd)
        control = bits_to_int(values[position:position + CONTROL_BITS])
        position += CONTROL_BITS
        seq = bits_to_int(values[position:position + SEQ_BITS])
        position += SEQ_BITS
        data = bits_to_int(values[position:position + DATA_BITS])
        position += DATA_BITS
        received_crc = bits_to_int(values[position:position + CRC_BITS])
        frame = cls(control, data, seq, crc_algorithm, received_crc)
        if verify and not frame.verify_crc():
            raise FrameError(
                "CRC mismatch: received 0x%02X, expected 0x%02X"
                % (received_crc, frame.compute_crc())
            )
        return frame

    @classmethod
    def from_wire_bits(cls, wire_bits, data_lanes, crc_algorithm=DEFAULT_CRC, verify=True):
        if not isinstance(data_lanes, int) or data_lanes < 1:
            raise ValueError("data_lanes must be a positive integer")
        expected_length = FRAME_BITS + ((-FRAME_BITS) % data_lanes)
        values = _validate_bits(wire_bits, expected_length)
        padding = values[FRAME_BITS:]
        if any(padding):
            raise FrameError("Padding bits must be zero")
        return cls.from_bits(values[:FRAME_BITS], crc_algorithm, verify)

    @classmethod
    def ack(cls, seq, crc_algorithm=DEFAULT_CRC):
        return cls(CONTROL_ACK, 0, seq, crc_algorithm)

    @classmethod
    def nack(cls, seq, crc_algorithm=DEFAULT_CRC):
        return cls(CONTROL_NACK, 0, seq, crc_algorithm)
