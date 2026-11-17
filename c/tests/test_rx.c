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

static void append_bits(uint8_t *samples, size_t *length, size_t capacity,
                        const uint8_t *bits, size_t bit_count)
{
    size_t index;

    for (index = 0; index < bit_count; index++) {
        if (*length + 2u > capacity) {
            return;
        }
        samples[(*length)++] = bits[index];
        samples[(*length)++] = bits[index];
    }
}

static void build_samples(uint8_t *samples, size_t *length, size_t capacity,
                          phy_crc_algorithm_t crc, uint8_t control, uint8_t seq, uint8_t data)
{
    phy_frame_t frame;
    uint8_t bits[PHY_FRAME_BITS];

    phy_frame_init(&frame, control, seq, data, crc);
    phy_frame_to_bits(&frame, bits);
    append_bits(samples, length, capacity, bits, PHY_FRAME_BITS);
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

static void test_frame_with_leading_garbage(void)
{
    static const uint8_t garbage[] = { 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 1 };
    phy_fake_rx_channel_t channel;
    phy_frame_t frame;
    phy_frame_status_t status;
    uint8_t wire[PHY_FRAME_BITS];
    uint8_t samples[PHY_FRAME_BITS * 2u * 2u];
    size_t length = 0;
    int result;

    append_bits(samples, &length, sizeof(samples), garbage, sizeof(garbage));
    build_samples(samples, &length, sizeof(samples), PHY_CRC_SMBUS,
                  PHY_CONTROL_DATA, 7, 'z');

    CHECK(phy_fake_rx_channel_init(&channel, samples, length));
    result = phy_rx_frame(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                          &frame, &status, wire, sizeof(wire), -1);
    CHECK(result == 1);
    CHECK(status == PHY_FRAME_OK);
    CHECK(frame.seq == 7u);
    CHECK(frame.data == 'z');
}

static void test_frame_resync_after_bad_syn(void)
{
    phy_fake_rx_channel_t channel;
    phy_frame_t frame;
    phy_frame_status_t status;
    uint8_t wire[PHY_FRAME_BITS];
    uint8_t bits[PHY_FRAME_BITS];
    uint8_t samples[PHY_FRAME_BITS * 2u * 3u];
    size_t length = 0;
    int result;

    {
        phy_frame_t source;
        phy_frame_init(&source, PHY_CONTROL_DATA, 1, 'a', PHY_CRC_SMBUS);
        phy_frame_to_bits(&source, bits);
    }
    bits[0] ^= 1u;
    append_bits(samples, &length, sizeof(samples), bits, PHY_FRAME_BITS);
    build_samples(samples, &length, sizeof(samples), PHY_CRC_SMBUS,
                  PHY_CONTROL_DATA, 2, 'b');

    CHECK(phy_fake_rx_channel_init(&channel, samples, length));
    result = phy_rx_frame(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                          &frame, &status, wire, sizeof(wire), -1);
    CHECK(result == 1);
    CHECK(status == PHY_FRAME_OK);
    CHECK(frame.seq == 2u);
    CHECK(frame.data == 'b');
}

static void test_frame_resync_after_bit_slip(void)
{
    static const uint8_t extra_bit[] = { 0 };
    phy_fake_rx_channel_t channel;
    phy_frame_t frame;
    phy_frame_status_t status;
    uint8_t wire[PHY_FRAME_BITS];
    uint8_t samples[PHY_FRAME_BITS * 2u * 3u];
    size_t length = 0;
    int result;

    build_samples(samples, &length, sizeof(samples), PHY_CRC_SMBUS,
                  PHY_CONTROL_DATA, 3, 'c');
    append_bits(samples, &length, sizeof(samples), extra_bit, sizeof(extra_bit));
    build_samples(samples, &length, sizeof(samples), PHY_CRC_SMBUS,
                  PHY_CONTROL_DATA, 4, 'd');

    CHECK(phy_fake_rx_channel_init(&channel, samples, length));
    result = phy_rx_frame(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                          &frame, &status, wire, sizeof(wire), -1);
    CHECK(result == 1);
    CHECK(status == PHY_FRAME_OK);
    CHECK(frame.seq == 3u);
    CHECK(frame.data == 'c');

    result = phy_rx_frame(phy_fake_rx_channel_base(&channel), PHY_CRC_SMBUS,
                          &frame, &status, wire, sizeof(wire), -1);
    CHECK(result == 1);
    CHECK(status == PHY_FRAME_OK);
    CHECK(frame.seq == 4u);
    CHECK(frame.data == 'd');
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
    test_frame_with_leading_garbage();
    test_frame_resync_after_bad_syn();
    test_frame_resync_after_bit_slip();
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
