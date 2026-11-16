#define _POSIX_C_SOURCE 200809L

#include "phy_net.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int wait_readable(int fd, int timeout_ms)
{
    struct pollfd descriptor;
    int result;

    descriptor.fd = fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    result = poll(&descriptor, 1, timeout_ms);
    if (result <= 0) {
        return 0;
    }
    return (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

int phy_net_listen(const char *host, int port)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *item;
    char port_text[16];
    int listen_fd = -1;

    if (host == NULL || port < 0 || port > 65535) {
        return -1;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    snprintf(port_text, sizeof(port_text), "%d", port);

    if (getaddrinfo(host, port_text, &hints, &results) != 0) {
        return -1;
    }
    for (item = results; item != NULL; item = item->ai_next) {
        int option = 1;

        listen_fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (listen_fd < 0) {
            continue;
        }
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
        if (bind(listen_fd, item->ai_addr, item->ai_addrlen) == 0 && listen(listen_fd, 8) == 0) {
            break;
        }
        close(listen_fd);
        listen_fd = -1;
    }
    freeaddrinfo(results);
    return listen_fd;
}

int phy_net_accept(int listen_fd)
{
    return accept(listen_fd, NULL, NULL);
}

int phy_net_connect(const char *host, int port)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *item;
    char port_text[16];
    int fd = -1;

    if (host == NULL || port <= 0 || port > 65535) {
        return -1;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%d", port);

    if (getaddrinfo(host, port_text, &hints, &results) != 0) {
        return -1;
    }
    for (item = results; item != NULL; item = item->ai_next) {
        fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, item->ai_addr, item->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(results);
    return fd;
}

int phy_net_send_all(int fd, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t offset = 0;

    while (offset < length) {
        ssize_t sent = send(fd, bytes + offset, length - offset, 0);
        if (sent <= 0) {
            return 0;
        }
        offset += (size_t)sent;
    }
    return 1;
}

int phy_net_send_wire(int fd, int lanes, const uint8_t *wire, size_t wire_length)
{
    char line[128];
    size_t offset;
    size_t i;

    if (wire == NULL || wire_length == 0 || wire_length > PHY_MAX_WIRE_BITS) {
        return 0;
    }
    offset = (size_t)snprintf(line, sizeof(line), "F %d ", lanes);
    if (offset >= sizeof(line) || offset + wire_length + 1u > sizeof(line)) {
        return 0;
    }
    for (i = 0; i < wire_length; i++) {
        line[offset + i] = wire[i] ? '1' : '0';
    }
    line[offset + wire_length] = '\n';
    return phy_net_send_all(fd, line, offset + wire_length + 1u);
}

int phy_net_send_frame(int fd, const phy_frame_t *frame, int lanes)
{
    uint8_t wire[PHY_MAX_WIRE_BITS];
    size_t wire_length = 0;

    if (frame == NULL) {
        return 0;
    }
    phy_frame_to_wire_bits(frame, lanes, wire, &wire_length);
    return phy_net_send_wire(fd, lanes, wire, wire_length);
}

void phy_net_reader_init(phy_net_reader_t *reader, int fd)
{
    if (reader == NULL) {
        return;
    }
    reader->fd = fd;
    reader->start = 0;
    reader->end = 0;
}

int phy_net_read_line(phy_net_reader_t *reader, char *line, size_t capacity, int timeout_ms)
{
    if (reader == NULL || line == NULL || capacity == 0) {
        return -1;
    }

    for (;;) {
        char *newline;
        size_t available = reader->end - reader->start;
        size_t line_length;

        newline = (char *)memchr(reader->buffer + reader->start, '\n', available);
        if (newline != NULL) {
            line_length = (size_t)(newline - (reader->buffer + reader->start));
            if (line_length >= capacity) {
                line_length = capacity - 1u;
            }
            memcpy(line, reader->buffer + reader->start, line_length);
            line[line_length] = '\0';
            reader->start = (size_t)(newline - reader->buffer) + 1u;
            if (reader->start == reader->end) {
                reader->start = 0;
                reader->end = 0;
            }
            return 1;
        }

        if (reader->start > 0) {
            memmove(reader->buffer, reader->buffer + reader->start, available);
            reader->start = 0;
            reader->end = available;
        }
        if (reader->end == sizeof(reader->buffer)) {
            return -1;
        }
        if (!wait_readable(reader->fd, timeout_ms)) {
            return 0;
        }

        {
            ssize_t received = recv(reader->fd, reader->buffer + reader->end,
                                    sizeof(reader->buffer) - reader->end, 0);
            if (received <= 0) {
                return -1;
            }
            reader->end += (size_t)received;
        }
    }
}

int phy_net_recv_frame(phy_net_reader_t *reader, int lanes, phy_crc_algorithm_t algorithm,
                       phy_frame_t *frame, phy_frame_status_t *status,
                       uint8_t *wire, size_t wire_capacity, size_t *wire_length,
                       int timeout_ms)
{
    char line[128];
    char bits_text[PHY_MAX_WIRE_BITS + 2u];
    int received_lanes = 0;
    int parsed;
    size_t bits_length;
    size_t i;

    if (frame == NULL || status == NULL || wire == NULL || wire_length == NULL ||
        wire_capacity < PHY_MAX_WIRE_BITS) {
        return -1;
    }
    *wire_length = 0;
    memset(frame, 0, sizeof(*frame));
    *status = PHY_FRAME_ERR_ARGUMENT;

    parsed = phy_net_read_line(reader, line, sizeof(line), timeout_ms);
    if (parsed <= 0) {
        return parsed;
    }
    if (sscanf(line, "F %d %51s", &received_lanes, bits_text) != 2) {
        return 1;
    }
    if (received_lanes != lanes) {
        return 1;
    }
    bits_length = strlen(bits_text);
    if (bits_length != phy_wire_length(lanes) || bits_length > PHY_MAX_WIRE_BITS) {
        return 1;
    }
    for (i = 0; i < bits_length; i++) {
        if (bits_text[i] != '0' && bits_text[i] != '1') {
            return 1;
        }
        wire[i] = (uint8_t)(bits_text[i] - '0');
    }
    *wire_length = bits_length;
    *status = phy_frame_from_wire_bits(wire, bits_length, lanes, algorithm, 1, frame);
    return 1;
}
