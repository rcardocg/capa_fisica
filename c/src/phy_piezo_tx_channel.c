#define _POSIX_C_SOURCE 200809L

#include "phy_piezo_tx_channel.h"

#include <string.h>
#include <time.h>

static void sleep_us(long microseconds)
{
    struct timespec delay;

    if (microseconds <= 0) {
        return;
    }
    delay.tv_sec = microseconds / 1000000L;
    delay.tv_nsec = (microseconds % 1000000L) * 1000L;
    nanosleep(&delay, NULL);
}

static void sleep_ms(int milliseconds)
{
    sleep_us((long)milliseconds * 1000L);
}

static int burst(phy_piezo_tx_channel_t *tx, int high_ms)
{
    long total_us = (long)high_ms * 1000L;
    long half_us = 500000L / (long)tx->carrier_hz;
    long period_us = half_us * 2L;
    long cycles = period_us > 0 ? total_us / period_us : 0;
    long index;

    if (half_us < 1) {
        half_us = 1;
        period_us = 2;
        cycles = total_us / period_us;
    }

    for (index = 0; index < cycles; index++) {
        if (phy_gpio_write(&tx->data, 1) != 0) {
            return 0;
        }
        sleep_us(half_us);
        if (phy_gpio_write(&tx->data, 0) != 0) {
            return 0;
        }
        sleep_us(half_us);
    }
    sleep_us(total_us - cycles * period_us);
    return 1;
}

static int piezo_tx_send_bits(phy_channel_t *channel, const uint8_t *bits,
                              size_t length)
{
    phy_piezo_tx_channel_t *tx = (phy_piezo_tx_channel_t *)channel->context;
    size_t index;

    if (tx == NULL || bits == NULL || length == 0) {
        return 0;
    }

    for (index = 0; index < length; index++) {
        int pulse;

        if (phy_gpio_write(&tx->data, 0) != 0) {
            return 0;
        }
        for (pulse = 0; pulse < 2; pulse++) {
            if (phy_gpio_write(&tx->clock, 1) != 0) {
                return 0;
            }
            if (bits[index]) {
                if (!burst(tx, tx->high_ms)) {
                    return 0;
                }
            } else {
                sleep_ms(tx->high_ms);
            }
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

static void piezo_tx_close(phy_channel_t *channel)
{
    phy_piezo_tx_channel_t *tx = (phy_piezo_tx_channel_t *)channel->context;

    if (tx == NULL) {
        return;
    }
    phy_gpio_close(&tx->data);
    phy_gpio_close(&tx->clock);
}

static const phy_channel_ops_t piezo_tx_ops = {
    "piezo-tx",
    piezo_tx_send_bits,
    piezo_tx_close
};

int phy_piezo_tx_channel_open(phy_piezo_tx_channel_t *channel, int clock_gpio,
                              int data_gpio, int clock_hz, int carrier_hz)
{
    int half_ms;

    if (channel == NULL || clock_hz < 1 || carrier_hz < 1) {
        return 0;
    }
    memset(channel, 0, sizeof(*channel));
    channel->clock.gpio = -1;
    channel->data.gpio = -1;
    channel->base.context = channel;
    channel->clock_hz = clock_hz;
    channel->carrier_hz = carrier_hz;

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
        piezo_tx_close(&channel->base);
        return 0;
    }

    channel->base.ops = &piezo_tx_ops;
    channel->base.context = channel;
    return 1;
}

phy_channel_t *phy_piezo_tx_channel_base(phy_piezo_tx_channel_t *channel)
{
    return channel == NULL ? NULL : &channel->base;
}
