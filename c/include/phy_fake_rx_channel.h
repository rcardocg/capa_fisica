#ifndef PHY_FAKE_RX_CHANNEL_H
#define PHY_FAKE_RX_CHANNEL_H

#include "phy_rx_channel.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    phy_rx_channel_t base;
    const uint8_t *samples;
    size_t length;
    size_t position;
    int idle;
} phy_fake_rx_channel_t;

int phy_fake_rx_channel_init(phy_fake_rx_channel_t *channel, const uint8_t *samples, size_t length);
phy_rx_channel_t *phy_fake_rx_channel_base(phy_fake_rx_channel_t *channel);
size_t phy_fake_rx_samples_from_bits(const uint8_t *bits, size_t bit_count,
                                     uint8_t *samples, size_t capacity);

#endif
