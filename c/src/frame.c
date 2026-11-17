#include "phy_protocol.h"

#include <string.h>

static void bits_set(uint8_t *bits, size_t offset, unsigned int width, uint32_t value)
{
    size_t i;

    for (i = 0; i < (size_t)width; i++) {
        unsigned int shift = (unsigned int)(width - 1u - (unsigned int)i);
        bits[offset + i] = (uint8_t)((value >> shift) & 1u);
    }
}

static uint32_t bits_get(const uint8_t *bits, size_t offset, unsigned int width)
{
    uint32_t value = 0;
    unsigned int i;

    for (i = 0; i < width; i++) {
        value = (value << 1) | (uint32_t)(bits[offset + i] & 1u);
    }
    return value;
}

const char *phy_control_name(uint8_t control)
{
    switch (control) {
    case PHY_CONTROL_DATA:
        return "DATA";
    case PHY_CONTROL_ACK:
        return "ACK";
    case PHY_CONTROL_NACK:
        return "NACK";
    case PHY_CONTROL_HELLO:
        return "HELLO";
    case PHY_CONTROL_CONFIG:
        return "CONFIG";
    case PHY_CONTROL_STATUS:
        return "STATUS";
    case PHY_CONTROL_SYN:
        return "SYN";
    case PHY_CONTROL_SYN_ACK:
        return "SYN-ACK";
    case PHY_CONTROL_ABORT:
        return "ABORT";
    default:
        return "DESCONOCIDO";
    }
}

const char *phy_frame_status_text(phy_frame_status_t status)
{
    switch (status) {
    case PHY_FRAME_OK:
        return "OK";
    case PHY_FRAME_ERR_LENGTH:
        return "longitud invalida";
    case PHY_FRAME_ERR_SYN:
        return "SYN invalido";
    case PHY_FRAME_ERR_SFD:
        return "SFD invalido";
    case PHY_FRAME_ERR_CRC:
        return "CRC invalido";
    case PHY_FRAME_ERR_PADDING:
        return "relleno invalido";
    case PHY_FRAME_ERR_ARGUMENT:
        return "argumento invalido";
    default:
        return "error desconocido";
    }
}

size_t phy_wire_length(int lanes)
{
    size_t padding;

    if (lanes < 1 || lanes > PHY_MAX_LANES) {
        return 0;
    }
    padding = (size_t)(lanes - (PHY_FRAME_BITS % (size_t)lanes));
    if (padding == (size_t)lanes) {
        padding = 0;
    }
    return (size_t)PHY_FRAME_BITS + padding;
}

void phy_frame_init(phy_frame_t *frame, uint8_t control, uint8_t seq, uint8_t data, phy_crc_algorithm_t algorithm)
{
    if (frame == NULL) {
        return;
    }
    frame->control = (uint8_t)(control & 0x0Fu);
    frame->seq = (uint8_t)(seq & 0x0Fu);
    frame->data = data;
    frame->algorithm = algorithm;
    frame->crc = phy_frame_crc(frame);
}

uint8_t phy_frame_crc(const phy_frame_t *frame)
{
    uint8_t payload[2];

    if (frame == NULL) {
        return 0;
    }
    payload[0] = (uint8_t)(((frame->control & 0x0Fu) << 4) | (frame->seq & 0x0Fu));
    payload[1] = frame->data;
    return phy_crc8(payload, sizeof(payload), frame->algorithm);
}

int phy_frame_verify_crc(const phy_frame_t *frame)
{
    if (frame == NULL) {
        return 0;
    }
    return phy_frame_crc(frame) == frame->crc;
}

void phy_frame_to_bits(const phy_frame_t *frame, uint8_t *bits)
{
    if (frame == NULL || bits == NULL) {
        return;
    }
    bits_set(bits, 0, PHY_SYN_BITS, PHY_SYN_PATTERN);
    bits_set(bits, 16, PHY_SFD_BITS, PHY_SFD_PATTERN);
    bits_set(bits, 20, PHY_CONTROL_BITS, frame->control);
    bits_set(bits, 24, PHY_SEQ_BITS, frame->seq);
    bits_set(bits, 28, PHY_DATA_BITS, frame->data);
    bits_set(bits, 36, PHY_CRC_BITS, frame->crc);
}

void phy_frame_to_wire_bits(const phy_frame_t *frame, int lanes, uint8_t *bits, size_t *length)
{
    size_t wire_length;
    size_t i;

    if (length == NULL) {
        return;
    }
    *length = 0;
    if (frame == NULL || bits == NULL) {
        return;
    }
    wire_length = phy_wire_length(lanes);
    if (wire_length == 0) {
        return;
    }
    phy_frame_to_bits(frame, bits);
    for (i = PHY_FRAME_BITS; i < wire_length; i++) {
        bits[i] = 0;
    }
    *length = wire_length;
}

phy_frame_status_t phy_frame_from_bits(const uint8_t *bits, size_t length, phy_crc_algorithm_t algorithm, int verify, phy_frame_t *out)
{
    phy_frame_t frame;
    size_t i;

    if (bits == NULL || out == NULL || length != PHY_FRAME_BITS) {
        return PHY_FRAME_ERR_LENGTH;
    }
    if (algorithm < 0 || algorithm >= PHY_CRC_ALGORITHM_COUNT) {
        return PHY_FRAME_ERR_ARGUMENT;
    }
    for (i = 0; i < length; i++) {
        if (bits[i] > 1u) {
            return PHY_FRAME_ERR_ARGUMENT;
        }
    }

    frame.algorithm = algorithm;
    frame.control = (uint8_t)bits_get(bits, 20, PHY_CONTROL_BITS);
    frame.seq = (uint8_t)bits_get(bits, 24, PHY_SEQ_BITS);
    frame.data = (uint8_t)bits_get(bits, 28, PHY_DATA_BITS);
    frame.crc = (uint8_t)bits_get(bits, 36, PHY_CRC_BITS);
    *out = frame;

    if (bits_get(bits, 0, PHY_SYN_BITS) != PHY_SYN_PATTERN) {
        return PHY_FRAME_ERR_SYN;
    }
    if (bits_get(bits, 16, PHY_SFD_BITS) != PHY_SFD_PATTERN) {
        return PHY_FRAME_ERR_SFD;
    }
    if (verify && !phy_frame_verify_crc(&frame)) {
        return PHY_FRAME_ERR_CRC;
    }
    return PHY_FRAME_OK;
}

phy_frame_status_t phy_frame_from_wire_bits(const uint8_t *bits, size_t length, int lanes, phy_crc_algorithm_t algorithm, int verify, phy_frame_t *out)
{
    size_t expected;
    size_t i;

    expected = phy_wire_length(lanes);
    if (expected == 0) {
        return PHY_FRAME_ERR_ARGUMENT;
    }
    if (bits == NULL || out == NULL || length != expected) {
        return PHY_FRAME_ERR_LENGTH;
    }
    for (i = PHY_FRAME_BITS; i < length; i++) {
        if (bits[i] > 1u) {
            return PHY_FRAME_ERR_ARGUMENT;
        }
        if (bits[i] != 0u) {
            return PHY_FRAME_ERR_PADDING;
        }
    }
    return phy_frame_from_bits(bits, PHY_FRAME_BITS, algorithm, verify, out);
}
