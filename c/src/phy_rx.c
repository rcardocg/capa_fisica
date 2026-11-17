#include "phy_rx.h"

#include "phy_sample.h"

#include <stdio.h>
#include <string.h>

static int receive_bit(phy_rx_channel_t *channel, uint8_t *bit, int timeout_ms)
{
    int sample_one = 0;
    int sample_two = 0;
    int result;

    result = phy_rx_channel_recv_sample(channel, &sample_one, timeout_ms);
    if (result != 1) {
        return result < 0 ? -1 : 0;
    }
    result = phy_rx_channel_recv_sample(channel, &sample_two, timeout_ms);
    if (result != 1) {
        return result < 0 ? -1 : 0;
    }
    *bit = (uint8_t)(phy_sample_decode_pair(sample_one, sample_two) ? 1u : 0u);
    return 1;
}

int phy_rx_frame(phy_rx_channel_t *channel, phy_crc_algorithm_t crc,
                 phy_frame_t *frame, phy_frame_status_t *status,
                 uint8_t *wire, size_t wire_capacity, int timeout_ms)
{
    uint8_t window[PHY_FRAME_BITS];
    size_t filled = 0;

    if (channel == NULL || frame == NULL || status == NULL || wire == NULL ||
        wire_capacity < PHY_FRAME_BITS) {
        return -1;
    }
    if (crc < 0 || crc >= PHY_CRC_ALGORITHM_COUNT) {
        return -1;
    }

    if (phy_rx_channel_wait_idle(channel, timeout_ms) != 1) {
        return 0;
    }

    for (;;) {
        uint8_t bit = 0;
        int result = receive_bit(channel, &bit, timeout_ms);

        if (result != 1) {
            return result < 0 ? -1 : 0;
        }

        if (filled < PHY_FRAME_BITS) {
            window[filled++] = bit;
        } else {
            memmove(window, window + 1, PHY_FRAME_BITS - 1u);
            window[PHY_FRAME_BITS - 1u] = bit;
        }

        if (filled < PHY_FRAME_BITS) {
            continue;
        }

        {
            phy_frame_t candidate;
            phy_frame_status_t candidate_status;

            candidate_status = phy_frame_from_bits(window, PHY_FRAME_BITS, crc,
                                                   0, &candidate);
            if (candidate_status != PHY_FRAME_OK) {
                continue;
            }

            memcpy(wire, window, PHY_FRAME_BITS);
            *frame = candidate;
            if (phy_frame_verify_crc(&candidate)) {
                *status = PHY_FRAME_OK;
            } else {
                *status = PHY_FRAME_ERR_CRC;
            }
            return 1;
        }
    }
}

static void trace_frame(const phy_frame_t *frame, phy_frame_status_t status)
{
    fprintf(stderr,
            "[RX] control=%s seq=%u data=0x%02X '%c' crc=0x%02X status=%s\n",
            phy_control_name(frame->control),
            (unsigned int)frame->seq,
            (unsigned int)frame->data,
            (frame->data >= 32u && frame->data < 127u) ? (char)frame->data : '.',
            (unsigned int)frame->crc,
            phy_frame_status_text(status));
    fflush(stderr);
}

int phy_rx_message(phy_rx_channel_t *channel, phy_crc_algorithm_t crc,
                   phy_encoding_t encoding, const phy_rx_options_t *options,
                   uint8_t *output, size_t output_capacity, int timeout_ms)
{
    size_t written = 0;

    if (channel == NULL || options == NULL || output == NULL || output_capacity == 0) {
        return -1;
    }

    for (;;) {
        phy_frame_t frame;
        phy_frame_status_t status;
        uint8_t wire[PHY_FRAME_BITS];
        uint8_t decoded[4];
        size_t decoded_length = 0;
        size_t index;
        int result;

        result = phy_rx_frame(channel, crc, &frame, &status,
                              wire, sizeof(wire), timeout_ms);
        if (result < 0) {
            return -1;
        }
        if (result == 0) {
            return 0;
        }

        if (options->trace) {
            trace_frame(&frame, status);
        }
        if (status != PHY_FRAME_OK) {
            continue;
        }
        if (frame.control != PHY_CONTROL_DATA) {
            continue;
        }

        if (!phy_decode_output(&frame.data, 1, encoding, decoded, sizeof(decoded), &decoded_length)) {
            continue;
        }
        for (index = 0; index < decoded_length; index++) {
            if (written + 1u >= output_capacity) {
                output[written] = '\0';
                return -1;
            }
            output[written] = decoded[index];
            if (output[written] == '\n') {
                output[written] = '\0';
                return 1;
            }
            written++;
        }
    }
}
