#include "phy_fake_rx_channel.h"
#include "phy_rx.h"

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

static void build_samples(uint8_t *samples, size_t *length, size_t capacity,
                          phy_crc_algorithm_t crc, uint8_t control, uint8_t seq, uint8_t data)
{
    phy_frame_t frame;
    uint8_t bits[PHY_FRAME_BITS];
    size_t index;

    phy_frame_init(&frame, control, seq, data, crc);
    phy_frame_to_bits(&frame, bits);
    for (index = 0; index < PHY_FRAME_BITS; index++) {
        if (*length + 2u > capacity) {
            return;
        }
        samples[(*length)++] = bits[index];
        samples[(*length)++] = bits[index];
    }
}

static void test_frame_ok(void)
{
    phy_fake_rx_channel_t channel;
    phy_frame_t frame;
    phy_frame_status_t status;
    uint8_t wire[PHY_FRAME_BITS];
    uint8_t samples[PHY_FRAME_BITS * 2u];
    size_t length = 0;
    int result;

    build_samples(samples, &length, sizeof(samples), PHY_CRC_SMBUS, PHY_CONTROL_DATA, 2, 'h');
    CHECK(length == PHY_FRAME_BITS * 2u);
    CHECK(phy_fake_rx_channel_init(&channel, samples, length));

    result = phy_rx_frame(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                          &frame, &status, wire, sizeof(wire), -1);
    CHECK(result == 1);
    CHECK(status == PHY_FRAME_OK);
    CHECK(frame.control == PHY_CONTROL_DATA);
    CHECK(frame.seq == 2u);
    CHECK(frame.data == 'h');
    CHECK(phy_frame_verify_crc(&frame));
}

static void test_frame_crc_error(void)
{
    phy_fake_rx_channel_t channel;
    phy_frame_t frame;
    phy_frame_status_t status;
    uint8_t wire[PHY_FRAME_BITS];
    uint8_t bits[PHY_FRAME_BITS];
    uint8_t samples[PHY_FRAME_BITS * 2u];
    size_t length = 0;
    size_t index;
    int result;

    {
        phy_frame_t source;
        phy_frame_init(&source, PHY_CONTROL_DATA, 4, 'x', PHY_CRC_SMBUS);
        phy_frame_to_bits(&source, bits);
    }
    bits[28] ^= 1u;

    for (index = 0; index < PHY_FRAME_BITS; index++) {
        samples[length++] = bits[index];
        samples[length++] = bits[index];
    }

    CHECK(phy_fake_rx_channel_init(&channel, samples, length));
    result = phy_rx_frame(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                          &frame, &status, wire, sizeof(wire), -1);
    CHECK(result == 1);
    CHECK(status == PHY_FRAME_ERR_CRC);
}

static void test_frame_syn_error(void)
{
    phy_fake_rx_channel_t channel;
    phy_frame_t frame;
    phy_frame_status_t status;
    uint8_t wire[PHY_FRAME_BITS];
    uint8_t bits[PHY_FRAME_BITS];
    uint8_t samples[PHY_FRAME_BITS * 2u];
    size_t length = 0;
    size_t index;
    int result;

    {
        phy_frame_t source;
        phy_frame_init(&source, PHY_CONTROL_DATA, 1, 'a', PHY_CRC_SMBUS);
        phy_frame_to_bits(&source, bits);
    }
    bits[0] ^= 1u;

    for (index = 0; index < PHY_FRAME_BITS; index++) {
        samples[length++] = bits[index];
        samples[length++] = bits[index];
    }

    CHECK(phy_fake_rx_channel_init(&channel, samples, length));
    result = phy_rx_frame(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                          &frame, &status, wire, sizeof(wire), -1);
    CHECK(result == 1);
    CHECK(status == PHY_FRAME_ERR_SYN);
}

static void test_message_ascii(void)
{
    static const uint8_t text[] = "hola\n";
    phy_fake_rx_channel_t channel;
    phy_rx_options_t options;
    uint8_t output[64];
    uint8_t samples[PHY_FRAME_BITS * 2u * 8u];
    size_t length = 0;
    size_t index;
    int result;

    for (index = 0; index < sizeof(text) - 1u; index++) {
        build_samples(samples, &length, sizeof(samples), PHY_CRC_SMBUS,
                      PHY_CONTROL_DATA, (uint8_t)index, text[index]);
    }

    CHECK(phy_fake_rx_channel_init(&channel, samples, length));
    options.trace = 0;
    result = phy_rx_message(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                            PHY_ENCODING_LATIN1, &options, output, sizeof(output), -1);
    CHECK(result == 1);
    CHECK(strcmp((const char *)output, "hola") == 0);
}

static void test_message_latin1(void)
{
    uint8_t bytes[2] = { 0xF1u, '\n' };
    phy_fake_rx_channel_t channel;
    phy_rx_options_t options;
    uint8_t output[64];
    uint8_t samples[PHY_FRAME_BITS * 2u * 4u];
    size_t length = 0;
    size_t index;
    int result;

    for (index = 0; index < sizeof(bytes); index++) {
        build_samples(samples, &length, sizeof(samples), PHY_CRC_SMBUS,
                      PHY_CONTROL_DATA, (uint8_t)index, bytes[index]);
    }

    CHECK(phy_fake_rx_channel_init(&channel, samples, length));
    options.trace = 0;
    result = phy_rx_message(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                            PHY_ENCODING_LATIN1, &options, output, sizeof(output), -1);
    CHECK(result == 1);
    CHECK(output[0] == 0xC3u && output[1] == 0xB1u && output[2] == '\0');
}

static void test_timeout_idle(void)
{
    phy_fake_rx_channel_t channel;
    phy_frame_t frame;
    phy_frame_status_t status;
    uint8_t wire[PHY_FRAME_BITS];
    uint8_t samples[PHY_FRAME_BITS * 2u];
    int result;

    CHECK(phy_fake_rx_channel_init(&channel, samples, sizeof(samples)));
    channel.idle = 0;
    result = phy_rx_frame(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                          &frame, &status, wire, sizeof(wire), -1);
    CHECK(result == 0);
}

static void test_timeout_samples(void)
{
    phy_fake_rx_channel_t channel;
    phy_frame_t frame;
    phy_frame_status_t status;
    uint8_t wire[PHY_FRAME_BITS];
    int result;

    CHECK(phy_fake_rx_channel_init(&channel, NULL, 0));
    result = phy_rx_frame(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                          &frame, &status, wire, sizeof(wire), -1);
    CHECK(result == 0);
}

int main(void)
{
    test_frame_ok();
    test_frame_crc_error();
    test_frame_syn_error();
    test_message_ascii();
    test_message_latin1();
    test_timeout_idle();
    test_timeout_samples();

    if (failures != 0) {
        fprintf(stderr, "%d pruebas fallaron\n", failures);
        return 1;
    }
    printf("OK: recepcion de tramas y mensajes\n");
    return 0;
}
