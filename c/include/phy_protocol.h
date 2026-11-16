#ifndef PHY_PROTOCOL_H
#define PHY_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define PHY_SYN_PATTERN 0xAAAAu
#define PHY_SYN_BITS 16
#define PHY_SFD_PATTERN 0x0Du
#define PHY_SFD_BITS 4
#define PHY_CONTROL_BITS 4
#define PHY_SEQ_BITS 4
#define PHY_DATA_BITS 8
#define PHY_CRC_BITS 8
#define PHY_FRAME_BITS 44
#define PHY_MAX_LANES 7
#define PHY_MAX_WIRE_BITS (PHY_FRAME_BITS + PHY_MAX_LANES - 1)

#define PHY_CONTROL_DATA 0x0u
#define PHY_CONTROL_ACK 0x1u
#define PHY_CONTROL_NACK 0x2u
#define PHY_CONTROL_HELLO 0x3u
#define PHY_CONTROL_CONFIG 0x4u
#define PHY_CONTROL_STATUS 0x5u
#define PHY_CONTROL_ABORT 0xFu

typedef enum {
    PHY_CRC_SMBUS = 0,
    PHY_CRC_MAXIM_DOW,
    PHY_CRC_SAE_J1850,
    PHY_CRC_AUTOSAR,
    PHY_CRC_ALGORITHM_COUNT
} phy_crc_algorithm_t;

typedef enum {
    PHY_FRAME_OK = 0,
    PHY_FRAME_ERR_LENGTH,
    PHY_FRAME_ERR_SYN,
    PHY_FRAME_ERR_SFD,
    PHY_FRAME_ERR_CRC,
    PHY_FRAME_ERR_PADDING,
    PHY_FRAME_ERR_ARGUMENT
} phy_frame_status_t;

typedef enum {
    PHY_ENCODING_UTF8 = 0,
    PHY_ENCODING_LATIN1
} phy_encoding_t;

typedef struct {
    uint8_t control;
    uint8_t seq;
    uint8_t data;
    uint8_t crc;
    phy_crc_algorithm_t algorithm;
} phy_frame_t;

const char *phy_crc_name(phy_crc_algorithm_t algorithm);
int phy_crc_parse(const char *name, phy_crc_algorithm_t *out);
uint8_t phy_crc8(const uint8_t *data, size_t length, phy_crc_algorithm_t algorithm);

const char *phy_control_name(uint8_t control);
size_t phy_wire_length(int lanes);
void phy_frame_init(phy_frame_t *frame, uint8_t control, uint8_t seq, uint8_t data, phy_crc_algorithm_t algorithm);
uint8_t phy_frame_crc(const phy_frame_t *frame);
int phy_frame_verify_crc(const phy_frame_t *frame);
void phy_frame_to_bits(const phy_frame_t *frame, uint8_t *bits);
void phy_frame_to_wire_bits(const phy_frame_t *frame, int lanes, uint8_t *bits, size_t *length);
phy_frame_status_t phy_frame_from_bits(const uint8_t *bits, size_t length, phy_crc_algorithm_t algorithm, int verify, phy_frame_t *out);
phy_frame_status_t phy_frame_from_wire_bits(const uint8_t *bits, size_t length, int lanes, phy_crc_algorithm_t algorithm, int verify, phy_frame_t *out);
const char *phy_frame_status_text(phy_frame_status_t status);

const char *phy_encoding_name(phy_encoding_t encoding);
int phy_encoding_parse(const char *name, phy_encoding_t *out);
int phy_encode_line(const uint8_t *input, size_t input_length, phy_encoding_t encoding, uint8_t *output, size_t output_capacity, size_t *output_length);
int phy_decode_output(const uint8_t *input, size_t input_length, phy_encoding_t encoding, uint8_t *output, size_t output_capacity, size_t *output_length);
int phy_utf8_valid(const uint8_t *input, size_t length);

#endif
