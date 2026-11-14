import unittest

from capa_fisica.protocol.crc import available_algorithms
from capa_fisica.protocol.frame import (
    CONTROL_ACK,
    CONTROL_DATA,
    CONTROL_NACK,
    FRAME_BITS,
    SFD_PATTERN,
    SYN_PATTERN,
    Frame,
    FrameError,
    bits_to_int,
)


class FrameTests(unittest.TestCase):
    def test_field_layout(self):
        frame = Frame(CONTROL_DATA, 0x41, 3, "smbus")
        bits = frame.to_bits()
        self.assertEqual(len(bits), FRAME_BITS)
        position = 0
        self.assertEqual(bits_to_int(bits[position:position + 16]), SYN_PATTERN)
        position += 16
        self.assertEqual(bits_to_int(bits[position:position + 4]), SFD_PATTERN)
        position += 4
        self.assertEqual(bits_to_int(bits[position:position + 4]), CONTROL_DATA)
        position += 4
        self.assertEqual(bits_to_int(bits[position:position + 4]), 3)
        position += 4
        self.assertEqual(bits_to_int(bits[position:position + 8]), 0x41)
        position += 8
        self.assertEqual(bits_to_int(bits[position:position + 8]), frame.crc)

    def test_roundtrip(self):
        for algorithm in available_algorithms():
            original = Frame(CONTROL_DATA, 0x7F, 9, algorithm)
            rebuilt = Frame.from_bits(original.to_bits(), algorithm)
            self.assertEqual(rebuilt.control, original.control)
            self.assertEqual(rebuilt.seq, original.seq)
            self.assertEqual(rebuilt.data, original.data)
            self.assertEqual(rebuilt.crc, original.crc)
            self.assertTrue(rebuilt.verify_crc())

    def test_bit_flip_is_detected(self):
        frame = Frame(CONTROL_DATA, 0x42, 1, "autosar")
        bits = frame.to_bits()
        bits[30] ^= 1
        with self.assertRaises(FrameError):
            Frame.from_bits(bits, "autosar")

    def test_invalid_syn_is_detected(self):
        frame = Frame(CONTROL_DATA, 0x42, 1, "smbus")
        bits = frame.to_bits()
        bits[0] ^= 1
        with self.assertRaises(FrameError):
            Frame.from_bits(bits, "smbus")

    def test_invalid_sfd_is_detected(self):
        frame = Frame(CONTROL_DATA, 0x42, 1, "smbus")
        bits = frame.to_bits()
        bits[19] ^= 1
        with self.assertRaises(FrameError):
            Frame.from_bits(bits, "smbus")

    def test_wire_padding_for_all_lane_counts(self):
        frame = Frame(CONTROL_DATA, 0x55, 6, "sae-j1850")
        base_bits = frame.to_bits()
        for lanes in range(2, 8):
            wire = frame.to_wire_bits(lanes)
            self.assertEqual(len(wire) % lanes, 0)
            self.assertGreaterEqual(len(wire), len(base_bits))
            padding = wire[len(base_bits):]
            self.assertTrue(all(bit == 0 for bit in padding))
            rebuilt = Frame.from_wire_bits(wire, lanes, "sae-j1850")
            self.assertEqual(rebuilt.data, 0x55)
            self.assertEqual(rebuilt.seq, 6)

    def test_nonzero_padding_is_rejected(self):
        frame = Frame(CONTROL_DATA, 0x10, 0, "smbus")
        wire = frame.to_wire_bits(3)
        wire[-1] = 1
        with self.assertRaises(FrameError):
            Frame.from_wire_bits(wire, 3, "smbus")

    def test_ack_and_nack(self):
        ack = Frame.ack(4, "maxim-dow")
        nack = Frame.nack(4, "maxim-dow")
        self.assertTrue(ack.is_ack())
        self.assertTrue(nack.is_nack())
        self.assertEqual(ack.control, CONTROL_ACK)
        self.assertEqual(nack.control, CONTROL_NACK)
        self.assertEqual(ack.data, 0)
        self.assertEqual(nack.data, 0)
        self.assertTrue(ack.verify_crc())
        self.assertTrue(nack.verify_crc())

    def test_invalid_values(self):
        with self.assertRaises(ValueError):
            Frame(0x10, 0, 0, "smbus")
        with self.assertRaises(ValueError):
            Frame(0, 0x100, 0, "smbus")
        with self.assertRaises(ValueError):
            Frame(0, 0, 0x10, "smbus")

    def test_crc_coverage_changes_with_seq(self):
        first = Frame(CONTROL_DATA, 0x20, 1, "autosar")
        second = Frame(CONTROL_DATA, 0x20, 2, "autosar")
        self.assertNotEqual(first.crc, second.crc)

    def test_to_dict(self):
        frame = Frame(CONTROL_DATA, 0x41, 7, "smbus")
        data = frame.to_dict()
        self.assertEqual(data["control_name"], "DATA")
        self.assertEqual(data["seq"], 7)
        self.assertEqual(data["data"], 0x41)


if __name__ == "__main__":
    unittest.main()
