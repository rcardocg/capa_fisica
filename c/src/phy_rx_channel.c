#include "phy_rx_channel.h"

int phy_rx_channel_wait_idle(phy_rx_channel_t *channel, int timeout_ms)
{
    if (channel == NULL || channel->ops == NULL || channel->ops->wait_idle == NULL) {
        return -1;
    }
    return channel->ops->wait_idle(channel, timeout_ms);
}

int phy_rx_channel_recv_sample(phy_rx_channel_t *channel, int *sample, int timeout_ms)
{
    if (channel == NULL || channel->ops == NULL || channel->ops->recv_sample == NULL) {
        return -1;
    }
    return channel->ops->recv_sample(channel, sample, timeout_ms);
}

void phy_rx_channel_close(phy_rx_channel_t *channel)
{
    if (channel == NULL || channel->ops == NULL || channel->ops->close == NULL) {
        return;
    }
    channel->ops->close(channel);
}
