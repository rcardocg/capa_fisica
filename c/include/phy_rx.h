#ifndef PHY_RX_H
#define PHY_RX_H

#include "phy_protocol.h"
#include "phy_rx_channel.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int trace;
} phy_rx_options_t;

int phy_rx_frame(phy_rx_channel_t *channel, phy_crc_algorithm_t crc,
                 phy_frame_t *frame, phy_frame_status_t *status,
                 uint8_t *wire, size_t wire_capacity, int timeout_ms);

int phy_rx_message(phy_rx_channel_t *channel, phy_crc_algorithm_t crc,
                   phy_encoding_t encoding, const phy_rx_options_t *options,
                   uint8_t *output, size_t output_capacity, int timeout_ms);

#endif
