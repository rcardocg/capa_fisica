#define _POSIX_C_SOURCE 200809L

#include "phy_fake_link.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PHY_FAKE_LINK_DEFAULT_CAPACITY 65536u

static void deadline_from_now(struct timespec *deadline, int timeout_ms)
{
    clock_gettime(CLOCK_REALTIME, deadline);
    deadline->tv_sec += timeout_ms / 1000;
    deadline->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

static int pipe_init(phy_fake_pipe_t *pipe, size_t capacity)
{
    if (pipe == NULL || capacity == 0) {
        return 0;
    }
    memset(pipe, 0, sizeof(*pipe));
    pipe->samples = (uint8_t *)malloc(capacity);
    if (pipe->samples == NULL) {
        return 0;
    }
    pipe->capacity = capacity;
    if (pthread_mutex_init(&pipe->mutex, NULL) != 0) {
        free(pipe->samples);
        pipe->samples = NULL;
        return 0;
    }
    if (pthread_cond_init(&pipe->cond, NULL) != 0) {
        pthread_mutex_destroy(&pipe->mutex);
        free(pipe->samples);
        pipe->samples = NULL;
        return 0;
    }
    return 1;
}

static void pipe_destroy(phy_fake_pipe_t *pipe)
{
    if (pipe == NULL || pipe->samples == NULL) {
        return;
    }
    pthread_cond_destroy(&pipe->cond);
    pthread_mutex_destroy(&pipe->mutex);
    free(pipe->samples);
    pipe->samples = NULL;
}

static void pipe_close(phy_fake_pipe_t *pipe)
{
    if (pipe == NULL) {
        return;
    }
    pthread_mutex_lock(&pipe->mutex);
    pipe->closed = 1;
    pthread_cond_broadcast(&pipe->cond);
    pthread_mutex_unlock(&pipe->mutex);
}

static void sleep_pulse(int clock_hz)
{
    struct timespec delay;
    long nanoseconds;

    if (clock_hz <= 0) {
        return;
    }
    nanoseconds = 1000000000L / clock_hz;
    delay.tv_sec = nanoseconds / 1000000000L;
    delay.tv_nsec = nanoseconds % 1000000000L;
    nanosleep(&delay, NULL);
}

static int pipe_push(phy_fake_pipe_t *pipe, uint8_t sample)
{
    if (pipe == NULL) {
        return 0;
    }
    pthread_mutex_lock(&pipe->mutex);
    while (pipe->count == pipe->capacity && !pipe->closed) {
        pthread_cond_wait(&pipe->cond, &pipe->mutex);
    }
    if (pipe->closed) {
        pthread_mutex_unlock(&pipe->mutex);
        return 0;
    }
    pipe->samples[(pipe->head + pipe->count) % pipe->capacity] = sample ? 1u : 0u;
    pipe->count++;
    pthread_cond_broadcast(&pipe->cond);
    pthread_mutex_unlock(&pipe->mutex);
    return 1;
}

static int pipe_pop(phy_fake_pipe_t *pipe, int *sample, int timeout_ms)
{
    if (pipe == NULL || sample == NULL) {
        return -1;
    }

    pthread_mutex_lock(&pipe->mutex);
    while (pipe->count == 0 && !pipe->closed) {
        if (timeout_ms < 0) {
            pthread_cond_wait(&pipe->cond, &pipe->mutex);
        } else {
            struct timespec deadline;
            int result;

            deadline_from_now(&deadline, timeout_ms);
            result = pthread_cond_timedwait(&pipe->cond, &pipe->mutex, &deadline);
            if (result == ETIMEDOUT) {
                pthread_mutex_unlock(&pipe->mutex);
                return 0;
            }
        }
    }
    if (pipe->count > 0) {
        *sample = pipe->samples[pipe->head] ? 1 : 0;
        pipe->head = (pipe->head + 1u) % pipe->capacity;
        pipe->count--;
        pthread_cond_broadcast(&pipe->cond);
        pthread_mutex_unlock(&pipe->mutex);
        return 1;
    }
    pthread_mutex_unlock(&pipe->mutex);
    return -1;
}

static int endpoint_send_bits(phy_channel_t *channel, const uint8_t *bits, size_t length)
{
    phy_fake_endpoint_t *endpoint = (phy_fake_endpoint_t *)channel->context;
    size_t index;

    if (endpoint == NULL || bits == NULL || length == 0) {
        return 0;
    }
    for (index = 0; index < length; index++) {
        if (!pipe_push(endpoint->out, bits[index] ? 1u : 0u)) {
            return 0;
        }
        sleep_pulse(endpoint->clock_hz);
        if (!pipe_push(endpoint->out, bits[index] ? 1u : 0u)) {
            return 0;
        }
        sleep_pulse(endpoint->clock_hz);
    }
    return 1;
}

static void endpoint_tx_close(phy_channel_t *channel)
{
    (void)channel;
}

static int endpoint_wait_idle(phy_rx_channel_t *channel, int timeout_ms)
{
    phy_fake_endpoint_t *endpoint = (phy_fake_endpoint_t *)channel->context;
    int closed;

    (void)timeout_ms;
    if (endpoint == NULL) {
        return -1;
    }
    pthread_mutex_lock(&endpoint->in->mutex);
    closed = endpoint->in->closed;
    pthread_mutex_unlock(&endpoint->in->mutex);
    return closed ? -1 : 1;
}

static int endpoint_recv_sample(phy_rx_channel_t *channel, int *sample, int timeout_ms)
{
    phy_fake_endpoint_t *endpoint = (phy_fake_endpoint_t *)channel->context;

    if (endpoint == NULL) {
        return -1;
    }
    return pipe_pop(endpoint->in, sample, timeout_ms);
}

static void endpoint_rx_close(phy_rx_channel_t *channel)
{
    phy_fake_endpoint_t *endpoint = (phy_fake_endpoint_t *)channel->context;

    if (endpoint == NULL) {
        return;
    }
    pipe_close(endpoint->in);
}

static const phy_channel_ops_t endpoint_tx_ops = {
    "fake-link-tx",
    endpoint_send_bits,
    endpoint_tx_close
};

static const phy_rx_channel_ops_t endpoint_rx_ops = {
    "fake-link-rx",
    endpoint_wait_idle,
    endpoint_recv_sample,
    endpoint_rx_close
};

static void endpoint_init(phy_fake_endpoint_t *endpoint, phy_fake_pipe_t *out,
                          phy_fake_pipe_t *in)
{
    endpoint->out = out;
    endpoint->in = in;
    endpoint->clock_hz = 0;
    endpoint->tx.ops = &endpoint_tx_ops;
    endpoint->tx.context = endpoint;
    endpoint->rx.ops = &endpoint_rx_ops;
    endpoint->rx.context = endpoint;
}

int phy_fake_link_init(phy_fake_link_t *link, size_t capacity)
{
    if (link == NULL) {
        return 0;
    }
    if (capacity == 0) {
        capacity = PHY_FAKE_LINK_DEFAULT_CAPACITY;
    }
    memset(link, 0, sizeof(*link));
    if (!pipe_init(&link->a_to_b, capacity)) {
        return 0;
    }
    if (!pipe_init(&link->b_to_a, capacity)) {
        pipe_destroy(&link->a_to_b);
        return 0;
    }
    endpoint_init(&link->a, &link->a_to_b, &link->b_to_a);
    endpoint_init(&link->b, &link->b_to_a, &link->a_to_b);
    return 1;
}

int phy_fake_link_set_clock_hz(phy_fake_link_t *link, int clock_hz)
{
    if (link == NULL || clock_hz < 0) {
        return 0;
    }
    link->a.clock_hz = clock_hz;
    link->b.clock_hz = clock_hz;
    return 1;
}

void phy_fake_link_close(phy_fake_link_t *link)
{
    if (link == NULL) {
        return;
    }
    pipe_close(&link->a_to_b);
    pipe_close(&link->b_to_a);
    pipe_destroy(&link->a_to_b);
    pipe_destroy(&link->b_to_a);
}

phy_channel_t *phy_fake_link_a_tx(phy_fake_link_t *link)
{
    return link == NULL ? NULL : &link->a.tx;
}

phy_rx_channel_t *phy_fake_link_a_rx(phy_fake_link_t *link)
{
    return link == NULL ? NULL : &link->a.rx;
}

phy_channel_t *phy_fake_link_b_tx(phy_fake_link_t *link)
{
    return link == NULL ? NULL : &link->b.tx;
}

phy_rx_channel_t *phy_fake_link_b_rx(phy_fake_link_t *link)
{
    return link == NULL ? NULL : &link->b.rx;
}
