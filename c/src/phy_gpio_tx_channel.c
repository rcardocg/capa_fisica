#define _POSIX_C_SOURCE 200809L

#include "phy_gpio_tx_channel.h"

#include <string.h>
#include <time.h>

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

static int gpio_tx_send_bits(phy_channel_t *channel, const uint8_t *bits, size_t length)
{
    phy_gpio_tx_channel_t *tx = (phy_gpio_tx_channel_t *)channel->context;
    size_t index;

    if (tx == NULL || bits == NULL || length == 0) {
        return 0;
    }

    for (index = 0; index < length; index++) {
        int pulse;

        if (phy_gpio_write(&tx->data, bits[index] ? 1 : 0) != 0) {
            return 0;
        }
        for (pulse = 0; pulse < 2; pulse++) {
            if (phy_gpio_write(&tx->clock, 1) != 0) {
                return 0;
            }
            sleep_ms(tx->high_ms);
            if (phy_gpio_write(&tx->clock, 0) != 0) {
                return 0;
            }
            sleep_ms(tx->low_ms);
        }
    }

    if (phy_gpio_write(&tx->data, 0) != 0) {
        return 0;
    }
    return phy_gpio_write(&tx->clock, 0) == 0;
}

static void gpio_tx_close(phy_channel_t *channel)
{
    phy_gpio_tx_channel_t *tx = (phy_gpio_tx_channel_t *)channel->context;

    if (tx == NULL) {
        return;
    }
    phy_gpio_close(&tx->data);
    phy_gpio_close(&tx->clock);
}

static const phy_channel_ops_t gpio_tx_ops = {
    "gpio-tx",
    gpio_tx_send_bits,
    gpio_tx_close
};

int phy_gpio_tx_channel_open(phy_gpio_tx_channel_t *channel, int clock_gpio,
                             int data_gpio, int clock_hz)
{
    int half_ms;

    if (channel == NULL || clock_hz < 1) {
        return 0;
    }
    memset(channel, 0, sizeof(*channel));
    channel->clock.gpio = -1;
    channel->data.gpio = -1;
    channel->base.context = channel;
    channel->clock_hz = clock_hz;

    half_ms = 1000 / clock_hz;
    if (half_ms < 2) {
        half_ms = 2;
    }
    channel->high_ms = half_ms * 40 / 100;
    if (channel->high_ms < 1) {
        channel->high_ms = 1;
    }
    channel->low_ms = half_ms - channel->high_ms;
    if (channel->low_ms < 1) {
        channel->low_ms = 1;
    }

    if (phy_gpio_open_output(&channel->clock, clock_gpio) != 0) {
        return 0;
    }
    if (phy_gpio_open_output(&channel->data, data_gpio) != 0) {
        phy_gpio_close(&channel->clock);
        return 0;
    }
    if (phy_gpio_write(&channel->clock, 0) != 0 ||
        phy_gpio_write(&channel->data, 0) != 0) {
        gpio_tx_close(&channel->base);
        return 0;
    }

    channel->base.ops = &gpio_tx_ops;
    channel->base.context = channel;
    return 1;
}

phy_channel_t *phy_gpio_tx_channel_base(phy_gpio_tx_channel_t *channel)
{
    return channel == NULL ? NULL : &channel->base;
}
