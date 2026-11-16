#include "phy_channel.h"

int phy_channel_send_bits(phy_channel_t *channel, const uint8_t *bits, size_t length)
{
    if (channel == NULL || channel->ops == NULL || channel->ops->send_bits == NULL) {
        return 0;
    }
    return channel->ops->send_bits(channel, bits, length);
}

void phy_channel_close(phy_channel_t *channel)
{
    if (channel == NULL || channel->ops == NULL || channel->ops->close == NULL) {
        return;
    }
    channel->ops->close(channel);
}
