#define _POSIX_C_SOURCE 200809L

#include "phy_light_rx_channel.h"

#include "phy_analog.h"
#include "phy_sample.h"

#include <string.h>
#include <time.h>

static long now_ms(void)
{
    struct timespec timestamp;

    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (long)timestamp.tv_sec * 1000L + (long)(timestamp.tv_nsec / 1000000L);
}

static void sleep_ms(int milliseconds)
{
    struct timespec delay;

    if (milliseconds <= 0) {
        return;
    }
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    nanosleep(&delay, NULL);
}

static int wait_clock_level(phy_gpio_t *clock, int level, int timeout_ms)
{
    long deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 0);

    for (;;) {
        int value = 0;

        if (phy_gpio_read(clock, &value) != 0) {
            return -1;
        }
        if (value == level) {
            return 1;
        }
        if (timeout_ms >= 0 && now_ms() >= deadline) {
            return 0;
        }
        sleep_ms(1);
    }
}

static int light_wait_idle(phy_rx_channel_t *channel, int timeout_ms)
{
    phy_light_rx_channel_t *rx = (phy_light_rx_channel_t *)channel->context;
    int result;

    if (rx == NULL) {
        return -1;
    }
    result = wait_clock_level(&rx->clock, 0, timeout_ms);
    if (result <= 0) {
        return result;
    }
    return 1;
}

static int light_recv_sample(phy_rx_channel_t *channel, int *sample, int timeout_ms)
{
    phy_light_rx_channel_t *rx = (phy_light_rx_channel_t *)channel->context;
    int average = 0;
    int result;

    if (rx == NULL || sample == NULL) {
        return -1;
    }
    result = wait_clock_level(&rx->clock, 1, timeout_ms);
    if (result <= 0) {
        return result;
    }
    sleep_ms(rx->settle_ms);
    if (phy_analog_read_average(rx->adc_path, rx->adc_samples,
                               rx->adc_interval_ms, &average) != 0) {
        return -1;
    }
    *sample = phy_sample_classify(average, rx->previous_sample,
                                  rx->on_level, rx->off_level);
    rx->previous_sample = *sample;
    result = wait_clock_level(&rx->clock, 0, timeout_ms);
    if (result <= 0) {
        return result;
    }
    return 1;
}

static void light_close(phy_rx_channel_t *channel)
{
    phy_light_rx_channel_t *rx = (phy_light_rx_channel_t *)channel->context;

    if (rx == NULL) {
        return;
    }
    phy_gpio_close(&rx->clock);
}

static const phy_rx_channel_ops_t light_rx_ops = {
    "light-rx",
    light_wait_idle,
    light_recv_sample,
    light_close
};

int phy_light_rx_channel_open(phy_light_rx_channel_t *channel,
                              const phy_light_rx_channel_config_t *config)
{
    int half_ms;
    int average = 0;

    if (channel == NULL || config == NULL || config->clock_hz < 1 ||
        config->adc_samples <= 0) {
        return 0;
    }
    memset(channel, 0, sizeof(*channel));
    channel->clock.gpio = -1;
    channel->on_level = config->on_level;
    channel->off_level = config->off_level;
    channel->adc_samples = config->adc_samples;
    channel->adc_interval_ms = config->adc_interval_ms;
    channel->base.ops = &light_rx_ops;
    channel->base.context = channel;

    half_ms = 1000 / config->clock_hz;
    if (half_ms < 2) {
        half_ms = 2;
    }
    channel->settle_ms = half_ms * 40 / 100;
    if (channel->settle_ms < 1) {
        channel->settle_ms = 1;
    }

    if (!phy_analog_find_raw(config->ain, channel->adc_path,
                             sizeof(channel->adc_path))) {
        return 0;
    }
    if (phy_analog_read_average(channel->adc_path, channel->adc_samples,
                                channel->adc_interval_ms, &average) != 0) {
        return 0;
    }
    channel->previous_sample = phy_sample_classify(average, 0, channel->on_level,
                                                   channel->off_level);
    if (phy_gpio_open_input(&channel->clock, config->clock_gpio) != 0) {
        return 0;
    }
    return 1;
}

phy_rx_channel_t *phy_light_rx_channel_base(phy_light_rx_channel_t *channel)
{
    return channel == NULL ? NULL : &channel->base;
}
