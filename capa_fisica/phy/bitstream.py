from ..protocol.frame import (
    Frame,
    FrameError,
    FRAME_BITS,
    SFD_BITS,
    SFD_PATTERN,
    SYN_BITS,
    SYN_PATTERN,
    int_to_bits,
)


def syn_sfd_bits():
    return int_to_bits(SYN_PATTERN, SYN_BITS) + int_to_bits(SFD_PATTERN, SFD_BITS)


def normalize_bits(bits):
    values = []
    for bit in bits:
        if bit not in (0, 1, False, True):
            raise ValueError("Bit values must be 0 or 1")
        values.append(1 if bit else 0)
    return values


def split_chunks(wire_bits, data_lanes):
    if not isinstance(data_lanes, int) or data_lanes < 1:
        raise ValueError("data_lanes must be a positive integer")
    bits = normalize_bits(wire_bits)
    if len(bits) % data_lanes != 0:
        raise ValueError("Bit count must be a multiple of data_lanes")
    chunks = []
    for position in range(0, len(bits), data_lanes):
        chunks.append(bits[position:position + data_lanes])
    return chunks


def join_chunks(chunks):
    bits = []
    for chunk in chunks:
        bits.extend(normalize_bits(chunk))
    return bits


def frame_to_wire_bits(frame, data_lanes):
    return frame.to_wire_bits(data_lanes)


def wire_bits_to_frame(wire_bits, data_lanes, crc_algorithm="smbus", verify=True):
    return Frame.from_wire_bits(wire_bits, data_lanes, crc_algorithm, verify)


def find_frame(wire_bits, data_lanes, crc_algorithm="smbus", verify=True, start=0):
    if not isinstance(data_lanes, int) or data_lanes < 1:
        raise ValueError("data_lanes must be a positive integer")
    bits = normalize_bits(wire_bits)
    pattern = syn_sfd_bits()
    first = max(0, start)
    limit = len(bits) - FRAME_BITS
    index = first
    while index <= limit:
        if bits[index:index + len(pattern)] == pattern:
            candidate = bits[index:index + FRAME_BITS]
            try:
                frame = Frame.from_bits(candidate, crc_algorithm, verify=verify)
            except FrameError:
                index += 1
                continue
            return frame, index
        index += 1
    return None, -1
