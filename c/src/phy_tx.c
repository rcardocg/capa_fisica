#include "phy_tx.h"

#include <stdio.h>
#include <string.h>

static void print_frame(size_t index, size_t total, const phy_frame_t *frame, const uint8_t *bits)
{
    char wire[PHY_FRAME_BITS + 1u];
    size_t i;

    for (i = 0; i < PHY_FRAME_BITS; i++) {
        wire[i] = bits[i] ? '1' : '0';
    }
    wire[PHY_FRAME_BITS] = '\0';

    printf("[TX] frame=%zu/%zu control=%s seq=%u data=0x%02X '%c' crc=0x%02X\n",
           index + 1u, total, phy_control_name(frame->control),
           (unsigned int)frame->seq, (unsigned int)frame->data,
           (frame->data >= 32u && frame->data < 127u) ? (char)frame->data : '.',
           (unsigned int)frame->crc);
    printf("[TX] wire=%s\n", wire);
    fflush(stdout);
}

int phy_tx_message(phy_channel_t *channel, const char *message, const phy_tx_options_t *options)
{
    uint8_t payload[8192];
    size_t payload_length = 0;
    size_t index;
    int seq;

    if (channel == NULL || message == NULL || options == NULL) {
        return 0;
    }
    if (!phy_encode_line((const uint8_t *)message, strlen(message), options->encoding,
                         payload, sizeof(payload), &payload_length)) {
        fprintf(stderr, "No se pudo codificar el mensaje como %s\n",
                phy_encoding_name(options->encoding));
        return 0;
    }

    printf("[TX] mensaje bytes=%zu encoding=%s crc=%s\n",
           payload_length, phy_encoding_name(options->encoding),
           phy_crc_name(options->crc));
    fflush(stdout);

    seq = options->seq_start & 0x0F;
    for (index = 0; index < payload_length; index++) {
        phy_frame_t frame;
        uint8_t bits[PHY_FRAME_BITS];

        phy_frame_init(&frame, PHY_CONTROL_DATA, (uint8_t)seq, payload[index], options->crc);
        phy_frame_to_bits(&frame, bits);
        print_frame(index, payload_length, &frame, bits);

        if (!phy_channel_send_bits(channel, bits, PHY_FRAME_BITS)) {
            fprintf(stderr, "Error enviando el frame %zu por el canal %s\n",
                    index + 1u, channel->ops != NULL ? channel->ops->name : "desconocido");
            return 0;
        }
        seq = (seq + 1) & 0x0F;
    }
    return 1;
}
