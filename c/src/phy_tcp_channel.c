#include "phy_tcp_channel.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int tcp_channel_send_bits(phy_channel_t *channel, const uint8_t *bits, size_t length)
{
    phy_tcp_channel_t *tcp = (phy_tcp_channel_t *)channel->context;
    char line[PHY_MAX_WIRE_BITS + 16u];
    char response[64];
    size_t offset;
    size_t index;

    if (tcp == NULL || tcp->fd < 0 || bits == NULL || length == 0 ||
        length > PHY_MAX_WIRE_BITS) {
        return 0;
    }

    offset = (size_t)snprintf(line, sizeof(line), "BITS ");
    if (offset >= sizeof(line) || offset + length + 1u > sizeof(line)) {
        return 0;
    }
    for (index = 0; index < length; index++) {
        line[offset + index] = bits[index] ? '1' : '0';
    }
    line[offset + length] = '\n';

    if (!phy_net_send_all(tcp->fd, line, offset + length + 1u)) {
        return 0;
    }
    if (phy_net_read_line(&tcp->reader, response, sizeof(response), -1) <= 0) {
        return 0;
    }
    return strncmp(response, "OK BITS", 7) == 0;
}

static void tcp_channel_close(phy_channel_t *channel)
{
    phy_tcp_channel_t *tcp = (phy_tcp_channel_t *)channel->context;

    if (tcp != NULL && tcp->fd >= 0) {
        close(tcp->fd);
        tcp->fd = -1;
    }
}

static const phy_channel_ops_t tcp_channel_ops = {
    "beagle-tcp",
    tcp_channel_send_bits,
    tcp_channel_close
};

int phy_tcp_channel_open(phy_tcp_channel_t *channel, const char *host, int port)
{
    if (channel == NULL || host == NULL) {
        return 0;
    }
    memset(channel, 0, sizeof(*channel));
    channel->fd = phy_net_connect(host, port);
    if (channel->fd < 0) {
        return 0;
    }
    phy_net_reader_init(&channel->reader, channel->fd);
    channel->base.ops = &tcp_channel_ops;
    channel->base.context = channel;
    return 1;
}
