#include "phy_protocol.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++; \
        } \
    } while (0)

static void test_crc(void)
{
    static const uint8_t input[] = "123456789";

    CHECK(phy_crc8(input, sizeof(input) - 1u, PHY_CRC_SMBUS) == 0xF4u);
    CHECK(phy_crc8(input, sizeof(input) - 1u, PHY_CRC_MAXIM_DOW) == 0xA1u);
    CHECK(phy_crc8(input, sizeof(input) - 1u, PHY_CRC_SAE_J1850) == 0x4Bu);
    CHECK(phy_crc8(input, sizeof(input) - 1u, PHY_CRC_AUTOSAR) == 0xDFu);
}

static void test_names(void)
{
    phy_crc_algorithm_t crc;
    phy_encoding_t encoding;

    CHECK(phy_crc_parse("maxim-dow", &crc) && crc == PHY_CRC_MAXIM_DOW);
    CHECK(phy_crc_parse("SAE_J1850", &crc) && crc == PHY_CRC_SAE_J1850);
    CHECK(!phy_crc_parse("inexistente", &crc));
    CHECK(phy_encoding_parse("UTF8", &encoding) && encoding == PHY_ENCODING_UTF8);
    CHECK(phy_encoding_parse("extended-ascii", &encoding) && encoding == PHY_ENCODING_LATIN1);
    CHECK(!phy_encoding_parse("utf-16", &encoding));
}

static void test_frame_roundtrip(void)
{
    phy_crc_algorithm_t algorithm;
    int lanes;

    for (algorithm = PHY_CRC_SMBUS; algorithm < PHY_CRC_ALGORITHM_COUNT; algorithm++) {
        for (lanes = 1; lanes <= PHY_MAX_LANES; lanes++) {
            phy_frame_t source;
            phy_frame_t decoded;
            uint8_t wire[PHY_MAX_WIRE_BITS];
            size_t wire_length = 0;
            size_t expected_length = phy_wire_length(lanes);
            phy_frame_status_t status;
            size_t i;

            phy_frame_init(&source, PHY_CONTROL_DATA, 9, 0x5A, algorithm);
            phy_frame_to_wire_bits(&source, lanes, wire, &wire_length);
            CHECK(wire_length == expected_length);
            for (i = PHY_FRAME_BITS; i < wire_length; i++) {
                CHECK(wire[i] == 0);
            }
            status = phy_frame_from_wire_bits(wire, wire_length, lanes, algorithm, 1, &decoded);
            CHECK(status == PHY_FRAME_OK);
            CHECK(decoded.control == source.control);
            CHECK(decoded.seq == source.seq);
            CHECK(decoded.data == source.data);
            CHECK(decoded.crc == source.crc);
            CHECK(phy_frame_verify_crc(&decoded));
        }
    }
}

static void test_frame_errors(void)
{
    phy_frame_t source;
    phy_frame_t decoded;
    uint8_t wire[PHY_MAX_WIRE_BITS];
    size_t wire_length = 0;

    phy_frame_init(&source, PHY_CONTROL_ACK, 3, 0x42, PHY_CRC_SMBUS);
    phy_frame_to_wire_bits(&source, 3, wire, &wire_length);

    wire[43] ^= 1u;
    CHECK(phy_frame_from_wire_bits(wire, wire_length, 3, PHY_CRC_SMBUS, 1, &decoded) == PHY_FRAME_ERR_CRC);
    wire[43] ^= 1u;

    wire[0] ^= 1u;
    CHECK(phy_frame_from_wire_bits(wire, wire_length, 3, PHY_CRC_SMBUS, 1, &decoded) == PHY_FRAME_ERR_SYN);
    wire[0] ^= 1u;

    wire[16] ^= 1u;
    CHECK(phy_frame_from_wire_bits(wire, wire_length, 3, PHY_CRC_SMBUS, 1, &decoded) == PHY_FRAME_ERR_SFD);
    wire[16] ^= 1u;

    wire[44] = 1u;
    CHECK(phy_frame_from_wire_bits(wire, wire_length, 3, PHY_CRC_SMBUS, 1, &decoded) == PHY_FRAME_ERR_PADDING);
    wire[44] = 0u;

    CHECK(phy_frame_from_wire_bits(wire, wire_length, 2, PHY_CRC_SMBUS, 1, &decoded) == PHY_FRAME_ERR_LENGTH);
    CHECK(phy_frame_from_wire_bits(wire, wire_length, 3, PHY_CRC_SMBUS, 0, &decoded) == PHY_FRAME_OK);
}

static void test_presentation(void)
{
    static const uint8_t plain[] = "A";
    static const uint8_t enye[] = {0xC3u, 0xB1u};
    static const uint8_t manana[] = "Mañana";
    static const uint8_t latin_byte[] = {0xF1u};
    uint8_t output[64];
    size_t output_length = 0;

    CHECK(phy_encode_line(plain, sizeof(plain) - 1u, PHY_ENCODING_UTF8, output, sizeof(output), &output_length));
    CHECK(output_length == 2u && output[0] == 'A' && output[1] == '\n');

    CHECK(phy_encode_line(enye, sizeof(enye), PHY_ENCODING_UTF8, output, sizeof(output), &output_length));
    CHECK(output_length == 3u && output[0] == 0xC3u && output[1] == 0xB1u && output[2] == '\n');

    CHECK(phy_encode_line(manana, sizeof(manana) - 1u, PHY_ENCODING_LATIN1, output, sizeof(output), &output_length));
    CHECK(output_length == 7u);
    CHECK(output[0] == 'M' && output[1] == 'a' && output[2] == 0xF1u && output[6] == '\n');

    CHECK(phy_decode_output(latin_byte, sizeof(latin_byte), PHY_ENCODING_LATIN1, output, sizeof(output), &output_length));
    CHECK(output_length == 2u && output[0] == 0xC3u && output[1] == 0xB1u);

    CHECK(phy_utf8_valid(enye, sizeof(enye)));
    CHECK(!phy_utf8_valid(enye, 1));
}

int main(void)
{
    test_crc();
    test_names();
    test_frame_roundtrip();
    test_frame_errors();
    test_presentation();

    if (failures != 0) {
        fprintf(stderr, "%d pruebas fallaron\n", failures);
        return 1;
    }
    printf("OK: pruebas de CRC, trama y presentacion\n");
    return 0;
}
