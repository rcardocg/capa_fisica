import unittest

from capa_fisica.phy.bitstream import (
    find_frame,
    frame_to_wire_bits,
    join_chunks,
    split_chunks,
    syn_sfd_bits,
    wire_bits_to_frame,
)
from capa_fisica.protocol.frame import CONTROL_DATA, Frame


class BitstreamTests(unittest.TestCase):
    def test_syn_sfd_pattern(self):
        pattern = syn_sfd_bits()
        self.assertEqual(len(pattern), 20)
        self.assertEqual(pattern[:16], [1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0])
        self.assertEqual(pattern[16:], [1, 1, 0, 1])

    def test_split_and_join(self):
        frame = Frame(CONTROL_DATA, 0x41, 0, "smbus")
        wire = frame_to_wire_bits(frame, 3)
        chunks = split_chunks(wire, 3)
        self.assertEqual(len(chunks), 15)
        self.assertTrue(all(len(chunk) == 3 for chunk in chunks))
        self.assertEqual(join_chunks(chunks), wire)

    def test_wire_roundtrip(self):
        for lanes in range(2, 8):
            frame = Frame(CONTROL_DATA, 0x7E, 5, "autosar")
            wire = frame_to_wire_bits(frame, lanes)
            rebuilt = wire_bits_to_frame(wire, lanes, "autosar")
            self.assertEqual(rebuilt.data, 0x7E)
            self.assertEqual(rebuilt.seq, 5)
            self.assertTrue(rebuilt.verify_crc())

    def test_find_frame_after_junk(self):
        frame = Frame(CONTROL_DATA, 0x33, 2, "maxim-dow")
        wire = frame_to_wire_bits(frame, 3)
        stream = [1, 1, 0, 0, 1, 0, 1] + wire
        found, position = find_frame(stream, 3, "maxim-dow")
        self.assertIsNotNone(found)
        self.assertEqual(position, 7)
        self.assertEqual(found.data, 0x33)

    def test_find_frame_rejects_corrupted_crc(self):
        frame = Frame(CONTROL_DATA, 0x33, 2, "smbus")
        wire = frame_to_wire_bits(frame, 3)
        wire[43] ^= 1
        found, position = find_frame(wire, 3, "smbus")
        self.assertIsNone(found)
        self.assertEqual(position, -1)

    def test_split_requires_multiple_of_lanes(self):
        with self.assertRaises(ValueError):
            split_chunks([0, 1, 0, 1, 1], 3)

    def test_invalid_lane_count(self):
        frame = Frame(CONTROL_DATA, 0x00, 0, "smbus")
        with self.assertRaises(ValueError):
            frame_to_wire_bits(frame, 0)


if __name__ == "__main__":
    unittest.main()
