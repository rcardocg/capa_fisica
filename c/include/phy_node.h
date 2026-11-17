#ifndef PHY_NODE_H
#define PHY_NODE_H

#include "phy_channel.h"
#include "phy_protocol.h"
#include "phy_rx_channel.h"

typedef struct phy_node phy_node_t;

typedef struct {
    const char *name;
    phy_channel_t *tx;
    phy_rx_channel_t *rx;
    phy_crc_algorithm_t crc;
    phy_encoding_t encoding;
    const char *bind_host;
    int port;
    int rx_timeout_ms;
    int trace;
    int ack_enabled;
    int ack_timeout_ms;
    int ack_retries;
} phy_node_config_t;

int phy_node_start(phy_node_t **out, const phy_node_config_t *config);
void phy_node_stop(phy_node_t *node);
int phy_node_port(const phy_node_t *node);

#endif
