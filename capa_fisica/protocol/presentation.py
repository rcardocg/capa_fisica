import codecs

ENCODING_UTF8 = "utf-8"
ENCODING_LATIN1 = "latin-1"

ENCODING_ALIASES = {
    "utf8": ENCODING_UTF8,
    "utf_8": ENCODING_UTF8,
    "unicode": ENCODING_UTF8,
    "latin1": ENCODING_LATIN1,
    "latin_1": ENCODING_LATIN1,
    "iso-8859-1": ENCODING_LATIN1,
    "iso8859-1": ENCODING_LATIN1,
    "extended-ascii": ENCODING_LATIN1,
    "extended_ascii": ENCODING_LATIN1,
}


def normalize_encoding(name):
    if name is None:
        raise ValueError("Encoding is required")
    key = str(name).strip().lower().replace(" ", "").replace("_", "-")
    if key in (ENCODING_UTF8, ENCODING_LATIN1):
        return key
    if key in ENCODING_ALIASES:
        return ENCODING_ALIASES[key]
    raise ValueError("Unsupported encoding: %s" % name)


def available_encodings():
    return (ENCODING_UTF8, ENCODING_LATIN1)


def encode_text(text, encoding=ENCODING_UTF8):
    if not isinstance(text, str):
        raise TypeError("Text must be a unicode string")
    chosen = normalize_encoding(encoding)
    if chosen == ENCODING_LATIN1:
        try:
            return text.encode(ENCODING_LATIN1)
        except UnicodeEncodeError:
            raise ValueError("Text contains characters outside Latin-1")
    return text.encode(ENCODING_UTF8)


def decode_bytes(data, encoding=ENCODING_UTF8, errors="strict"):
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError("Data must be bytes or bytearray")
    chosen = normalize_encoding(encoding)
    return bytes(data).decode(chosen, errors)


def message_payload(text, encoding=ENCODING_UTF8):
    return encode_text(text + "\n", encoding)


class IncrementalDecoder(object):
    def __init__(self, encoding=ENCODING_UTF8, errors="strict"):
        self.encoding = normalize_encoding(encoding)
        self.errors = errors
        if self.encoding == ENCODING_UTF8:
            self._decoder = codecs.getincrementaldecoder(ENCODING_UTF8)(errors)
        else:
            self._decoder = None

    def feed(self, data):
        if isinstance(data, int):
            data = bytes(bytearray([data]))
        if not isinstance(data, (bytes, bytearray)):
            raise TypeError("Data must be bytes, bytearray or integer")
        if self._decoder is not None:
            return self._decoder.decode(bytes(data), final=False)
        return bytes(data).decode(self.encoding, self.errors)

    def finish(self):
        if self._decoder is not None:
            return self._decoder.decode(b"", final=True)
        return ""

    def reset(self):
        if self.encoding == ENCODING_UTF8:
            self._decoder = codecs.getincrementaldecoder(ENCODING_UTF8)(self.errors)
