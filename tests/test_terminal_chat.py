import os
import subprocess
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "tools", "terminal_chat.py")


def run_pipeline(text, encoding="utf-8", crc="smbus", lanes=3, corrupt_frame=None):
    tx_command = [
        sys.executable,
        TOOL,
        "tx",
        "--encoding",
        encoding,
        "--crc",
        crc,
        "--lanes",
        str(lanes),
        "--quiet",
    ]
    rx_command = [
        sys.executable,
        TOOL,
        "rx",
        "--encoding",
        encoding,
        "--crc",
        crc,
        "--lanes",
        str(lanes),
    ]
    if corrupt_frame is not None:
        tx_command.extend(["--corrupt-frame", str(corrupt_frame)])
    tx_process = subprocess.Popen(
        tx_command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    rx_process = subprocess.Popen(
        rx_command,
        stdin=tx_process.stdout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    tx_process.stdout.close()
    tx_process.stdin.write(text.encode("utf-8"))
    tx_process.stdin.close()
    output, rx_error = rx_process.communicate()
    tx_error = tx_process.stderr.read()
    tx_process.stderr.close()
    tx_process.wait()
    return (
        output.decode("utf-8"),
        rx_error.decode("utf-8"),
        tx_error.decode("utf-8"),
    )


class TerminalChatTests(unittest.TestCase):
    def test_utf8_pipeline(self):
        text = u"Hola, se\u00f1or \U0001f600\n"
        output, error, tx_error = run_pipeline(text)
        self.assertEqual(output, text)
        self.assertNotIn("ERROR", error)
        self.assertEqual(tx_error, "")

    def test_utf8_three_and_four_byte_characters(self):
        text = u"\u4f60\u597d \U0001f680\n"
        output, error, tx_error = run_pipeline(text)
        self.assertEqual(output, text)
        self.assertNotIn("ERROR", error)
        self.assertEqual(tx_error, "")

    def test_latin1_pipeline(self):
        text = u"Ma\u00f1ana \u00e1rbol\n"
        output, error, tx_error = run_pipeline(text, encoding="latin-1")
        self.assertEqual(output, text)
        self.assertNotIn("ERROR", error)
        self.assertEqual(tx_error, "")

    def test_all_crc_algorithms(self):
        text = u"CRC\n"
        for algorithm in ("smbus", "maxim-dow", "sae-j1850", "autosar"):
            output, error, tx_error = run_pipeline(text, crc=algorithm)
            self.assertEqual(output, text)
            self.assertNotIn("ERROR", error)
            self.assertEqual(tx_error, "")

    def test_different_lane_counts(self):
        text = u"Lanes\n"
        for lanes in (2, 3, 4, 5, 6, 7):
            output, error, tx_error = run_pipeline(text, lanes=lanes)
            self.assertEqual(output, text)
            self.assertNotIn("ERROR", error)
            self.assertEqual(tx_error, "")

    def test_corrupted_frame_is_reported(self):
        output, error, tx_error = run_pipeline("abc\n", corrupt_frame=1)
        self.assertEqual(output, "ac\n")
        self.assertIn("FRAME ERROR", error)
        self.assertIn("CRC mismatch", error)
        self.assertEqual(tx_error, "")


if __name__ == "__main__":
    unittest.main()
