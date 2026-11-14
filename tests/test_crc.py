import unittest

from capa_fisica.protocol.crc import (
    algorithm_info,
    available_algorithms,
    crc8,
    crc8_hex,
    normalize_algorithm,
)


class CRC8Tests(unittest.TestCase):
    def test_check_vectors(self):
        payload = b"123456789"
        expected = {
            "smbus": 0xF4,
            "maxim-dow": 0xA1,
            "sae-j1850": 0x4B,
            "autosar": 0xDF,
        }
        for algorithm, value in expected.items():
            self.assertEqual(crc8(payload, algorithm), value)

    def test_all_algorithms_are_available(self):
        self.assertEqual(
            available_algorithms(),
            ("autosar", "maxim-dow", "sae-j1850", "smbus"),
        )

    def test_aliases(self):
        self.assertEqual(normalize_algorithm("MAXIM"), "maxim-dow")
        self.assertEqual(normalize_algorithm("sae_j1850"), "sae-j1850")
        self.assertEqual(normalize_algorithm("SMB"), "smbus")
        self.assertEqual(normalize_algorithm("autosar"), "autosar")

    def test_unknown_algorithm(self):
        with self.assertRaises(ValueError):
            normalize_algorithm("crc16")

    def test_accepts_bytearray(self):
        self.assertEqual(
            crc8(bytearray(b"123456789"), "smbus"),
            crc8(b"123456789", "smbus"),
        )

    def test_rejects_other_input(self):
        with self.assertRaises(TypeError):
            crc8("123456789", "smbus")

    def test_hex_format(self):
        self.assertEqual(crc8_hex(b"123456789", "autosar"), "DF")

    def test_algorithm_info(self):
        info = algorithm_info("smbus")
        self.assertEqual(info["poly"], 0x07)
        self.assertEqual(info["check"], 0xF4)
        info["poly"] = 0
        self.assertEqual(algorithm_info("smbus")["poly"], 0x07)


if __name__ == "__main__":
    unittest.main()
