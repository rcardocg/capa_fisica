#ifndef PHY_PIEZO_TX_CHANNEL_H
#define PHY_PIEZO_TX_CHANNEL_H

#include "phy_channel.h"
#include "phy_gpio.h"

typedef struct {
    phy_channel_t base;
    phy_gpio_t clock;
    phy_gpio_t data;
    int clock_hz;
    int carrier_hz;
    int high_ms;
    int low_ms;
} phy_piezo_tx_channel_t;

int phy_piezo_tx_channel_open(phy_piezo_tx_channel_t *channel, int clock_gpio,
                              int data_gpio, int clock_hz, int carrier_hz);
phy_channel_t *phy_piezo_tx_channel_base(phy_piezo_tx_channel_t *channel);

#endif
