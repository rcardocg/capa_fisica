#include "phy_protocol.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    uint8_t polynomial;
    uint8_t initial;
    uint8_t xor_out;
    int reflected;
} crc_preset_t;

static const crc_preset_t presets[PHY_CRC_ALGORITHM_COUNT] = {
    {0x07u, 0x00u, 0x00u, 0},
    {0x31u, 0x00u, 0x00u, 1},
    {0x1Du, 0xFFu, 0xFFu, 0},
    {0x2Fu, 0xFFu, 0xFFu, 0}
};

static int equals_ignore_case(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static uint8_t reflect8(uint8_t value)
{
    uint8_t result = 0;
    int i;

    for (i = 0; i < 8; i++) {
        if ((value & 1u) != 0u) {
            result |= (uint8_t)(1u << (7 - i));
        }
        value >>= 1;
    }
    return result;
}

const char *phy_crc_name(phy_crc_algorithm_t algorithm)
{
    switch (algorithm) {
    case PHY_CRC_SMBUS:
        return "smbus";
    case PHY_CRC_MAXIM_DOW:
        return "maxim-dow";
    case PHY_CRC_SAE_J1850:
        return "sae-j1850";
    case PHY_CRC_AUTOSAR:
        return "autosar";
    default:
        return "desconocido";
    }
}

int phy_crc_parse(const char *name, phy_crc_algorithm_t *out)
{
    if (name == NULL || out == NULL) {
        return 0;
    }
    if (equals_ignore_case(name, "smbus")) {
        *out = PHY_CRC_SMBUS;
        return 1;
    }
    if (equals_ignore_case(name, "maxim-dow") || equals_ignore_case(name, "maxim") ||
        equals_ignore_case(name, "maxim_dow")) {
        *out = PHY_CRC_MAXIM_DOW;
        return 1;
    }
    if (equals_ignore_case(name, "sae-j1850") || equals_ignore_case(name, "sae") ||
        equals_ignore_case(name, "sae_j1850")) {
        *out = PHY_CRC_SAE_J1850;
        return 1;
    }
    if (equals_ignore_case(name, "autosar")) {
        *out = PHY_CRC_AUTOSAR;
        return 1;
    }
    return 0;
}

uint8_t phy_crc8(const uint8_t *data, size_t length, phy_crc_algorithm_t algorithm)
{
    const crc_preset_t *preset;
    uint8_t crc;
    size_t i;
    int bit;

    if (algorithm < 0 || algorithm >= PHY_CRC_ALGORITHM_COUNT) {
        return 0;
    }
    if (data == NULL && length != 0) {
        return 0;
    }

    preset = &presets[algorithm];
    crc = preset->initial;

    for (i = 0; i < length; i++) {
        uint8_t byte = data[i];

        if (preset->reflected) {
            byte = reflect8(byte);
        }
        crc ^= byte;
        for (bit = 0; bit < 8; bit++) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8_t)((crc << 1) ^ preset->polynomial);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }

    if (preset->reflected) {
        crc = reflect8(crc);
    }
    return (uint8_t)(crc ^ preset->xor_out);
}
