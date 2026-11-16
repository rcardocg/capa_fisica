#ifndef PHY_GPIO_TX_CHANNEL_H
#define PHY_GPIO_TX_CHANNEL_H

#include "phy_channel.h"
#include "phy_gpio.h"

typedef struct {
    phy_channel_t base;
    phy_gpio_t clock;
    phy_gpio_t data;
    int clock_hz;
    int high_ms;
    int low_ms;
} phy_gpio_tx_channel_t;

int phy_gpio_tx_channel_open(phy_gpio_tx_channel_t *channel, int clock_gpio,
                             int data_gpio, int clock_hz);
phy_channel_t *phy_gpio_tx_channel_base(phy_gpio_tx_channel_t *channel);

#endif
