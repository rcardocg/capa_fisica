#ifndef PHY_TX_H
#define PHY_TX_H

#include "phy_channel.h"
#include "phy_protocol.h"

typedef struct {
    phy_encoding_t encoding;
    phy_crc_algorithm_t crc;
    int seq_start;
} phy_tx_options_t;

int phy_tx_message(phy_channel_t *channel, const char *message, const phy_tx_options_t *options);

#endif
