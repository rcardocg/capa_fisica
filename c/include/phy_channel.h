#ifndef PHY_CHANNEL_H
#define PHY_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

typedef struct phy_channel phy_channel_t;

typedef struct {
    const char *name;
    int (*send_bits)(phy_channel_t *channel, const uint8_t *bits, size_t length);
    void (*close)(phy_channel_t *channel);
} phy_channel_ops_t;

struct phy_channel {
    const phy_channel_ops_t *ops;
    void *context;
};

int phy_channel_send_bits(phy_channel_t *channel, const uint8_t *bits, size_t length);
void phy_channel_close(phy_channel_t *channel);

#endif
