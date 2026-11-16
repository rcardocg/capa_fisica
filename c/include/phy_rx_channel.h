#ifndef PHY_RX_CHANNEL_H
#define PHY_RX_CHANNEL_H

#include <stddef.h>

typedef struct phy_rx_channel phy_rx_channel_t;

typedef struct {
    const char *name;
    int (*wait_idle)(phy_rx_channel_t *channel, int timeout_ms);
    int (*recv_sample)(phy_rx_channel_t *channel, int *sample, int timeout_ms);
    void (*close)(phy_rx_channel_t *channel);
} phy_rx_channel_ops_t;

struct phy_rx_channel {
    const phy_rx_channel_ops_t *ops;
    void *context;
};

int phy_rx_channel_wait_idle(phy_rx_channel_t *channel, int timeout_ms);
int phy_rx_channel_recv_sample(phy_rx_channel_t *channel, int *sample, int timeout_ms);
void phy_rx_channel_close(phy_rx_channel_t *channel);

#endif
