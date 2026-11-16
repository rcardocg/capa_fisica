#include "phy_protocol.h"

#include <ctype.h>
#include <string.h>

static int utf8_decode_next(const uint8_t *input, size_t length, size_t *consumed, uint32_t *codepoint)
{
    uint8_t first;
    uint32_t value;
    uint32_t minimum;
    size_t extra;
    size_t i;

    if (input == NULL || length == 0 || consumed == NULL || codepoint == NULL) {
        return 0;
    }

    first = input[0];
    if (first < 0x80u) {
        *codepoint = first;
        *consumed = 1;
        return 1;
    }
    if ((first & 0xE0u) == 0xC0u) {
        extra = 1;
        value = (uint32_t)(first & 0x1Fu);
        minimum = 0x80u;
    } else if ((first & 0xF0u) == 0xE0u) {
        extra = 2;
        value = (uint32_t)(first & 0x0Fu);
        minimum = 0x800u;
    } else if ((first & 0xF8u) == 0xF0u) {
        extra = 3;
        value = (uint32_t)(first & 0x07u);
        minimum = 0x10000u;
    } else {
        return 0;
    }
    if (length < extra + 1u) {
        return 0;
    }
    for (i = 1; i <= extra; i++) {
        if ((input[i] & 0xC0u) != 0x80u) {
            return 0;
        }
        value = (value << 6) | (uint32_t)(input[i] & 0x3Fu);
    }
    if (value < minimum || value > 0x10FFFFu) {
        return 0;
    }
    if (value >= 0xD800u && value <= 0xDFFFu) {
        return 0;
    }
    *codepoint = value;
    *consumed = extra + 1u;
    return 1;
}

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

const char *phy_encoding_name(phy_encoding_t encoding)
{
    switch (encoding) {
    case PHY_ENCODING_UTF8:
        return "utf-8";
    case PHY_ENCODING_LATIN1:
        return "latin-1";
    default:
        return "desconocido";
    }
}

int phy_encoding_parse(const char *name, phy_encoding_t *out)
{
    if (name == NULL || out == NULL) {
        return 0;
    }
    if (equals_ignore_case(name, "utf-8") || equals_ignore_case(name, "utf8")) {
        *out = PHY_ENCODING_UTF8;
        return 1;
    }
    if (equals_ignore_case(name, "latin-1") || equals_ignore_case(name, "latin1") ||
        equals_ignore_case(name, "iso-8859-1") || equals_ignore_case(name, "extended-ascii")) {
        *out = PHY_ENCODING_LATIN1;
        return 1;
    }
    return 0;
}

int phy_utf8_valid(const uint8_t *input, size_t length)
{
    size_t offset = 0;

    if (input == NULL && length != 0) {
        return 0;
    }
    while (offset < length) {
        size_t consumed = 0;
        uint32_t codepoint = 0;

        if (!utf8_decode_next(input + offset, length - offset, &consumed, &codepoint)) {
            return 0;
        }
        offset += consumed;
    }
    return 1;
}

int phy_encode_line(const uint8_t *input, size_t input_length, phy_encoding_t encoding, uint8_t *output, size_t output_capacity, size_t *output_length)
{
    size_t input_offset = 0;
    size_t output_offset = 0;

    if (output == NULL || output_length == NULL || (input == NULL && input_length != 0)) {
        return 0;
    }
    *output_length = 0;

    if (encoding == PHY_ENCODING_UTF8) {
        if (!phy_utf8_valid(input, input_length)) {
            return 0;
        }
        if (output_capacity < input_length + 1u) {
            return 0;
        }
        if (input_length > 0) {
            memcpy(output, input, input_length);
        }
        output[input_length] = '\n';
        *output_length = input_length + 1u;
        return 1;
    }

    if (encoding != PHY_ENCODING_LATIN1) {
        return 0;
    }
    while (input_offset < input_length) {
        size_t consumed = 0;
        uint32_t codepoint = 0;

        if (!utf8_decode_next(input + input_offset, input_length - input_offset, &consumed, &codepoint)) {
            return 0;
        }
        if (codepoint > 0xFFu) {
            return 0;
        }
        if (output_offset + 1u >= output_capacity) {
            return 0;
        }
        output[output_offset++] = (uint8_t)codepoint;
        input_offset += consumed;
    }
    if (output_offset + 1u > output_capacity) {
        return 0;
    }
    output[output_offset++] = '\n';
    *output_length = output_offset;
    return 1;
}

int phy_decode_output(const uint8_t *input, size_t input_length, phy_encoding_t encoding, uint8_t *output, size_t output_capacity, size_t *output_length)
{
    size_t i;
    size_t offset = 0;

    if (output == NULL || output_length == NULL || (input == NULL && input_length != 0)) {
        return 0;
    }
    *output_length = 0;

    if (encoding == PHY_ENCODING_UTF8) {
        if (output_capacity < input_length) {
            return 0;
        }
        if (input_length > 0) {
            memcpy(output, input, input_length);
        }
        *output_length = input_length;
        return 1;
    }

    if (encoding != PHY_ENCODING_LATIN1) {
        return 0;
    }
    for (i = 0; i < input_length; i++) {
        if (input[i] < 0x80u) {
            if (offset + 1u > output_capacity) {
                *output_length = 0;
                return 0;
            }
            output[offset++] = input[i];
        } else {
            if (offset + 2u > output_capacity) {
                *output_length = 0;
                return 0;
            }
            output[offset++] = (uint8_t)(0xC0u | (input[i] >> 6));
            output[offset++] = (uint8_t)(0x80u | (input[i] & 0x3Fu));
        }
    }
    *output_length = offset;
    return 1;
}
