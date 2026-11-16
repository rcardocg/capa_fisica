#ifndef PHY_FAKE_LINK_H
#define PHY_FAKE_LINK_H

#include "phy_channel.h"
#include "phy_rx_channel.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *samples;
    size_t capacity;
    size_t head;
    size_t count;
    int closed;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} phy_fake_pipe_t;

typedef struct {
    phy_channel_t tx;
    phy_rx_channel_t rx;
    phy_fake_pipe_t *out;
    phy_fake_pipe_t *in;
    int clock_hz;
} phy_fake_endpoint_t;

typedef struct {
    phy_fake_pipe_t a_to_b;
    phy_fake_pipe_t b_to_a;
    phy_fake_endpoint_t a;
    phy_fake_endpoint_t b;
} phy_fake_link_t;

int phy_fake_link_init(phy_fake_link_t *link, size_t capacity);
int phy_fake_link_set_clock_hz(phy_fake_link_t *link, int clock_hz);
void phy_fake_link_close(phy_fake_link_t *link);
phy_channel_t *phy_fake_link_a_tx(phy_fake_link_t *link);
phy_rx_channel_t *phy_fake_link_a_rx(phy_fake_link_t *link);
phy_channel_t *phy_fake_link_b_tx(phy_fake_link_t *link);
phy_rx_channel_t *phy_fake_link_b_rx(phy_fake_link_t *link);

#endif
