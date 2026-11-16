#ifndef PHY_TCP_CHANNEL_H
#define PHY_TCP_CHANNEL_H

#include "phy_channel.h"
#include "phy_net.h"

typedef struct {
    phy_channel_t base;
    int fd;
    phy_net_reader_t reader;
} phy_tcp_channel_t;

int phy_tcp_channel_open(phy_tcp_channel_t *channel, const char *host, int port);

#endif
