#ifndef PHY_PIEZO_RX_CHANNEL_H
#define PHY_PIEZO_RX_CHANNEL_H

#include "phy_gpio.h"
#include "phy_piezo_dsp.h"
#include "phy_rx_channel.h"

#define PHY_PIEZO_MAX_SAMPLES 512

typedef struct {
    int clock_gpio;
    int ain;
    int clock_hz;
    int on_level;
    int off_level;
    int adc_samples;
    int adc_interval_ms;
    int baseline_shift;
} phy_piezo_rx_channel_config_t;

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
    phy_piezo_monitor_t monitor;
} phy_piezo_rx_channel_t;

int phy_piezo_rx_channel_open(phy_piezo_rx_channel_t *channel,
                              const phy_piezo_rx_channel_config_t *config);
phy_rx_channel_t *phy_piezo_rx_channel_base(phy_piezo_rx_channel_t *channel);

#endif
