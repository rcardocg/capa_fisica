#include "phy_fake_rx_channel.h"

static int fake_wait_idle(phy_rx_channel_t *channel, int timeout_ms)
{
    phy_fake_rx_channel_t *fake = (phy_fake_rx_channel_t *)channel->context;

    (void)timeout_ms;
    if (fake == NULL) {
        return -1;
    }
    return fake->idle ? 1 : 0;
}

static int fake_recv_sample(phy_rx_channel_t *channel, int *sample, int timeout_ms)
{
    phy_fake_rx_channel_t *fake = (phy_fake_rx_channel_t *)channel->context;

    (void)timeout_ms;
    if (fake == NULL || sample == NULL) {
        return -1;
    }
    if (fake->position >= fake->length) {
        return 0;
    }
    *sample = fake->samples[fake->position] ? 1 : 0;
    fake->position++;
    return 1;
}

static void fake_close(phy_rx_channel_t *channel)
{
    (void)channel;
}

static const phy_rx_channel_ops_t fake_rx_channel_ops = {
    "fake-rx",
    fake_wait_idle,
    fake_recv_sample,
    fake_close,
};

int phy_fake_rx_channel_init(phy_fake_rx_channel_t *channel, const uint8_t *samples, size_t length)
{
    if (channel == NULL) {
        return 0;
    }
    channel->samples = samples;
    channel->length = length;
    channel->position = 0;
    channel->idle = 1;
    channel->base.ops = &fake_rx_channel_ops;
    channel->base.context = channel;
    return 1;
}

phy_rx_channel_t *phy_fake_rx_channel_base(phy_fake_rx_channel_t *channel)
{
    return channel == NULL ? NULL : &channel->base;
}

size_t phy_fake_rx_samples_from_bits(const uint8_t *bits, size_t bit_count,
                                     uint8_t *samples, size_t capacity)
{
    size_t index;
    size_t written = 0;

    if (bits == NULL || samples == NULL || bit_count == 0) {
        return 0;
    }
    if (capacity < bit_count * 2u) {
        return 0;
    }
    for (index = 0; index < bit_count; index++) {
        samples[written++] = bits[index] ? 1u : 0u;
        samples[written++] = bits[index] ? 1u : 0u;
    }
    return written;
}
