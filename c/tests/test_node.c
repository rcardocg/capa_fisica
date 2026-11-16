#define _POSIX_C_SOURCE 200809L

#include "phy_fake_link.h"
#include "phy_net.h"
#include "phy_node.h"
#include "phy_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++; \
        } \
    } while (0)

typedef struct {
    int fd;
    phy_net_reader_t reader;
} client_t;

static long now_ms(void)
{
    struct timespec timestamp;

    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (long)timestamp.tv_sec * 1000L + (long)(timestamp.tv_nsec / 1000000L);
}

static void sleep_ms(int milliseconds)
{
    struct timespec delay;

    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    nanosleep(&delay, NULL);
}

static int client_open(client_t *client, int port)
{
    memset(client, 0, sizeof(*client));
    client->fd = phy_net_connect("127.0.0.1", port);
    if (client->fd < 0) {
        return 0;
    }
    phy_net_reader_init(&client->reader, client->fd);
    return 1;
}

static void client_close(client_t *client)
{
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
}

static int client_send_line(client_t *client, const char *line)
{
    char buffer[1024];
    int length = snprintf(buffer, sizeof(buffer), "%s\n", line);

    if (length < 0 || (size_t)length >= sizeof(buffer)) {
        return 0;
    }
    return phy_net_send_all(client->fd, buffer, (size_t)length);
}

static int client_command(client_t *client, const char *command,
                          char *line, size_t capacity, int timeout_ms)
{
    if (!client_send_line(client, command)) {
        return -1;
    }
    return phy_net_read_line(&client->reader, line, capacity, timeout_ms);
}

static int expect_command(client_t *client, const char *command, const char *expected)
{
    char line[512];
    int result = client_command(client, command, line, sizeof(line), 2000);

    if (result != 1) {
        fprintf(stderr, "sin respuesta a '%s'\n", command);
        return 0;
    }
    if (strncmp(line, expected, strlen(expected)) != 0) {
        fprintf(stderr, "respuesta inesperada a '%s': '%s'\n", command, line);
        return 0;
    }
    return 1;
}

static int wait_message(client_t *client, unsigned long *since,
                        const char *needle, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;
    char command[64];

    for (;;) {
        int received_next = 0;
        int found = 0;

        snprintf(command, sizeof(command), "POLL %lu", *since);
        if (!client_send_line(client, command)) {
            return 0;
        }
        while (!received_next) {
            char line[1024];
            long remaining = deadline - now_ms();
            int result;

            if (remaining <= 0) {
                return found;
            }
            result = phy_net_read_line(&client->reader, line, sizeof(line),
                                       (int)remaining);
            if (result < 0) {
                return found;
            }
            if (result == 0) {
                break;
            }
            if (strncmp(line, "NEXT ", 5) == 0) {
                *since = strtoul(line + 5, NULL, 10);
                received_next = 1;
            } else if (strstr(line, needle) != NULL) {
                found = 1;
            }
        }
        if (found) {
            return 1;
        }
        if (!received_next && now_ms() >= deadline) {
            return 0;
        }
        sleep_ms(20);
    }
}

static size_t build_text_bits(const char *text, phy_encoding_t encoding,
                              phy_crc_algorithm_t crc, char *output,
                              size_t capacity)
{
    uint8_t payload[256];
    size_t payload_length = 0;
    size_t index;
    size_t offset = 0;

    if (!phy_encode_line((const uint8_t *)text, strlen(text), encoding,
                         payload, sizeof(payload), &payload_length)) {
        return 0;
    }
    for (index = 0; index < payload_length; index++) {
        phy_frame_t frame;
        uint8_t bits[PHY_FRAME_BITS];
        size_t bit;

        phy_frame_init(&frame, PHY_CONTROL_DATA, (uint8_t)(index & 0x0Fu),
                       payload[index], crc);
        phy_frame_to_bits(&frame, bits);
        for (bit = 0; bit < PHY_FRAME_BITS; bit++) {
            if (offset + 1u >= capacity) {
                return 0;
            }
            output[offset++] = bits[bit] ? '1' : '0';
        }
    }
    output[offset] = '\0';
    return offset;
}

int main(void)
{
    phy_fake_link_t link;
    phy_node_t *node_a = NULL;
    phy_node_t *node_b = NULL;
    phy_node_config_t config;
    client_t client_a;
    client_t client_b;
    unsigned long since_a = 0;
    unsigned long since_b = 0;
    int link_ready = 0;
    int a_ready = 0;
    int b_ready = 0;

    memset(&client_a, 0, sizeof(client_a));
    memset(&client_b, 0, sizeof(client_b));
    client_a.fd = -1;
    client_b.fd = -1;

    CHECK(phy_fake_link_init(&link, 0));
    link_ready = 1;

    memset(&config, 0, sizeof(config));
    config.name = "A";
    config.tx = phy_fake_link_a_tx(&link);
    config.rx = phy_fake_link_a_rx(&link);
    config.crc = PHY_CRC_SMBUS;
    config.encoding = PHY_ENCODING_LATIN1;
    config.bind_host = "127.0.0.1";
    config.port = 0;
    config.rx_timeout_ms = 100;
    config.trace = 0;
    CHECK(phy_node_start(&node_a, &config));
    a_ready = node_a != NULL;

    config.name = "B";
    config.tx = phy_fake_link_b_tx(&link);
    config.rx = phy_fake_link_b_rx(&link);
    CHECK(phy_node_start(&node_b, &config));
    b_ready = node_b != NULL;

    if (a_ready && b_ready) {
        CHECK(client_open(&client_a, phy_node_port(node_a)));
        CHECK(client_open(&client_b, phy_node_port(node_b)));
    }

    if (client_a.fd >= 0 && client_b.fd >= 0) {
        char bits[512];

        CHECK(expect_command(&client_a, "PING", "PONG"));
        CHECK(expect_command(&client_a, "HELLO ana", "OK HELLO ana"));
        CHECK(expect_command(&client_a, "SEND ana hola B", "OK SEND"));
        CHECK(wait_message(&client_b, &since_b, " ana hola B", 2000));

        CHECK(expect_command(&client_b, "SEND bob desde B", "OK SEND"));
        CHECK(wait_message(&client_a, &since_a, " bob desde B", 2000));

        CHECK(expect_command(&client_a, "STATUS", "STATUS node=A"));

        CHECK(build_text_bits("ok", PHY_ENCODING_LATIN1, PHY_CRC_SMBUS,
                              bits, sizeof(bits)) > 0);
        {
            char command[600];

            snprintf(command, sizeof(command), "BITS %s", bits);
            CHECK(expect_command(&client_b, command, "OK BITS"));
            CHECK(wait_message(&client_a, &since_a, " bob ok", 2000));
        }

        CHECK(expect_command(&client_a, "SEND ana largo desde A", "OK SEND"));
        CHECK(expect_command(&client_b, "SEND bob largo desde B", "OK SEND"));
        CHECK(wait_message(&client_b, &since_b, " ana largo desde A", 2000));
        CHECK(wait_message(&client_a, &since_a, " bob largo desde B", 2000));
    }

    client_close(&client_a);
    client_close(&client_b);
    if (a_ready) {
        phy_node_stop(node_a);
    }
    if (b_ready) {
        phy_node_stop(node_b);
    }
    if (link_ready) {
        phy_fake_link_close(&link);
    }

    if (failures != 0) {
        fprintf(stderr, "%d pruebas fallaron\n", failures);
        return 1;
    }
    printf("OK: nodo A/B con enlace falso y API TCP\n");
    return 0;
}
