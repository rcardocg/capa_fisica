import unittest

from capa_fisica.protocol.presentation import (
    ENCODING_LATIN1,
    ENCODING_UTF8,
    IncrementalDecoder,
    available_encodings,
    decode_bytes,
    encode_text,
    message_payload,
    normalize_encoding,
)


class PresentationTests(unittest.TestCase):
    def test_available_encodings(self):
        self.assertEqual(available_encodings(), (ENCODING_UTF8, ENCODING_LATIN1))

    def test_encoding_aliases(self):
        self.assertEqual(normalize_encoding("UTF8"), ENCODING_UTF8)
        self.assertEqual(normalize_encoding("iso-8859-1"), ENCODING_LATIN1)
        self.assertEqual(normalize_encoding("extended_ascii"), ENCODING_LATIN1)
        with self.assertRaises(ValueError):
            normalize_encoding("utf-16")

    def test_utf8_lengths(self):
        self.assertEqual(len(encode_text("A", "utf-8")), 1)
        self.assertEqual(len(encode_text(u"\u00f1", "utf-8")), 2)
        self.assertEqual(len(encode_text(u"\u4f60", "utf-8")), 3)
        self.assertEqual(len(encode_text(u"\U0001f600", "utf-8")), 4)

    def test_utf8_roundtrip(self):
        text = u"Hola, se\u00f1or \u4f60\u597d \U0001f600"
        data = encode_text(text, "utf-8")
        self.assertEqual(decode_bytes(data, "utf-8"), text)

    def test_latin1_roundtrip(self):
        text = u"Ma\u00f1ana \u00e1rbol"
        data = encode_text(text, "latin-1")
        self.assertEqual(decode_bytes(data, "latin-1"), text)

    def test_latin1_full_byte_range(self):
        data = bytes(bytearray(range(256)))
        text = decode_bytes(data, "latin-1")
        self.assertEqual(len(text), 256)
        self.assertEqual(encode_text(text, "latin-1"), data)

    def test_latin1_rejects_unrepresentable_character(self):
        with self.assertRaises(ValueError):
            encode_text(u"\U0001f600", "latin-1")

    def test_message_payload_adds_newline(self):
        self.assertEqual(message_payload("hola", "utf-8"), b"hola\n")
        self.assertEqual(message_payload(u"a\u00f1o", "latin-1"), b"a\xf1o\n")

    def test_incremental_utf8_split_on_every_boundary(self):
        text = u"\U0001f600"
        data = encode_text(text, "utf-8")
        decoder = IncrementalDecoder("utf-8")
        output = []
        for value in bytearray(data):
            output.append(decoder.feed(value))
        output.append(decoder.finish())
        self.assertEqual("".join(output), text)

    def test_incremental_utf8_multiple_characters(self):
        text = u"a\u00f1\u4f60\U0001f600z"
        data = encode_text(text, "utf-8")
        decoder = IncrementalDecoder("utf-8")
        output = []
        for value in bytearray(data):
            output.append(decoder.feed(value))
        output.append(decoder.finish())
        self.assertEqual("".join(output), text)

    def test_incremental_latin1(self):
        decoder = IncrementalDecoder("latin-1")
        self.assertEqual(decoder.feed(0xF1), u"\u00f1")
        self.assertEqual(decoder.finish(), "")

    def test_invalid_utf8_raises(self):
        decoder = IncrementalDecoder("utf-8")
        with self.assertRaises(UnicodeDecodeError):
            decoder.feed(b"\xff")

    def test_invalid_utf8_replacement(self):
        decoder = IncrementalDecoder("utf-8", errors="replace")
        self.assertEqual(decoder.feed(b"\xff"), u"\ufffd")

    def test_reset_incremental_decoder(self):
        decoder = IncrementalDecoder("utf-8")
        decoder.feed(encode_text(u"\u00f1", "utf-8")[:1])
        decoder.reset()
        self.assertEqual(decoder.feed(b"A"), "A")


if __name__ == "__main__":
    unittest.main()
