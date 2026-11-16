#ifndef PHY_LIGHT_RX_CHANNEL_H
#define PHY_LIGHT_RX_CHANNEL_H

#include "phy_gpio.h"
#include "phy_rx_channel.h"

typedef struct {
    int clock_gpio;
    int ain;
    int clock_hz;
    int on_level;
    int off_level;
    int adc_samples;
    int adc_interval_ms;
} phy_light_rx_channel_config_t;

typedef struct {
    phy_rx_channel_t base;
    phy_gpio_t clock;
    char adc_path[256];
    int settle_ms;
    int on_level;
    int off_level;
    int adc_samples;
    int adc_interval_ms;
    int previous_sample;
} phy_light_rx_channel_t;

int phy_light_rx_channel_open(phy_light_rx_channel_t *channel,
                              const phy_light_rx_channel_config_t *config);
phy_rx_channel_t *phy_light_rx_channel_base(phy_light_rx_channel_t *channel);

#endif
