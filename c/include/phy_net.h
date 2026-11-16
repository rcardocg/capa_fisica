#ifndef PHY_NET_H
#define PHY_NET_H

#include "phy_protocol.h"

typedef struct {
    int fd;
    char buffer[512];
    size_t start;
    size_t end;
} phy_net_reader_t;

int phy_net_listen(const char *host, int port);
int phy_net_accept(int listen_fd);
int phy_net_connect(const char *host, int port);
int phy_net_send_all(int fd, const void *data, size_t length);
int phy_net_send_wire(int fd, int lanes, const uint8_t *wire, size_t wire_length);
int phy_net_send_frame(int fd, const phy_frame_t *frame, int lanes);
void phy_net_reader_init(phy_net_reader_t *reader, int fd);
int phy_net_read_line(phy_net_reader_t *reader, char *line, size_t capacity, int timeout_ms);
int phy_net_recv_frame(phy_net_reader_t *reader, int lanes, phy_crc_algorithm_t algorithm,
                       phy_frame_t *frame, phy_frame_status_t *status,
                       uint8_t *wire, size_t wire_capacity, size_t *wire_length,
                       int timeout_ms);

#endif
