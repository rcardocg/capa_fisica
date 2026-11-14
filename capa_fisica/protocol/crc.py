ALGORITHMS = {
    "smbus": {
        "poly": 0x07,
        "init": 0x00,
        "refin": False,
        "refout": False,
        "xorout": 0x00,
        "check": 0xF4,
    },
    "maxim-dow": {
        "poly": 0x31,
        "init": 0x00,
        "refin": True,
        "refout": True,
        "xorout": 0x00,
        "check": 0xA1,
    },
    "sae-j1850": {
        "poly": 0x1D,
        "init": 0xFF,
        "refin": False,
        "refout": False,
        "xorout": 0xFF,
        "check": 0x4B,
    },
    "autosar": {
        "poly": 0x2F,
        "init": 0xFF,
        "refin": False,
        "refout": False,
        "xorout": 0xFF,
        "check": 0xDF,
    },
}

ALIASES = {
    "maxim": "maxim-dow",
    "maxim_dow": "maxim-dow",
    "maximdow": "maxim-dow",
    "sae": "sae-j1850",
    "sae_j1850": "sae-j1850",
    "saej1850": "sae-j1850",
    "j1850": "sae-j1850",
    "smbus8": "smbus",
    "smb": "smbus",
}


def normalize_algorithm(name):
    if name is None:
        raise ValueError("CRC algorithm is required")
    key = str(name).strip().lower().replace(" ", "-")
    if key in ALGORITHMS:
        return key
    if key in ALIASES:
        return ALIASES[key]
    raise ValueError("Unknown CRC algorithm: %s" % name)


def available_algorithms():
    return tuple(sorted(ALGORITHMS.keys()))


def algorithm_info(name):
    return dict(ALGORITHMS[normalize_algorithm(name)])


def _reflect8(value):
    result = 0
    for index in range(8):
        result = (result << 1) | ((value >> index) & 1)
    return result


def crc8(data, algorithm="smbus"):
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError("CRC input must be bytes or bytearray")
    preset = ALGORITHMS[normalize_algorithm(algorithm)]
    crc = preset["init"]
    poly = preset["poly"]
    for item in bytearray(data):
        if preset["refin"]:
            item = _reflect8(item)
        crc ^= item
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ poly) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    if preset["refout"]:
        crc = _reflect8(crc)
    return (crc ^ preset["xorout"]) & 0xFF


def crc8_hex(data, algorithm="smbus"):
    return "%02X" % crc8(data, algorithm)
