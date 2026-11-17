#define _POSIX_C_SOURCE 200809L

#include "phy_node.h"

#include "phy_net.h"
#include "phy_rx.h"

#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PHY_NODE_MAX_JOBS 128u
#define PHY_NODE_MAX_BITS 512u
#define PHY_NODE_MAX_TEXT 2048u
#define PHY_NODE_RX_MESSAGE 4096u

typedef struct phy_node_job {
    struct phy_node_job *next;
    unsigned long id;
    int is_bits;
    uint8_t *payload;
    size_t length;
    char user[64];
} phy_node_job_t;

struct phy_node {
    char *name;
    char *bind_host;
    phy_channel_t *tx;
    phy_rx_channel_t *rx;
    phy_crc_algorithm_t crc;
    phy_encoding_t encoding;
    int rx_timeout_ms;
    int trace;

    int listen_fd;
    int port;
    int running;

    pthread_t api_thread;
    pthread_t rx_thread;
    pthread_t tx_thread;
    int api_started;
    int rx_started;
    int tx_started;

    pthread_mutex_t lock;
    pthread_cond_t tx_cond;
    pthread_mutex_t tx_lock;
    pthread_cond_t ack_cond;

    phy_node_job_t *job_head;
    phy_node_job_t *job_tail;
    size_t job_count;
    unsigned long next_job_id;

    char **events;
    size_t event_count;
    size_t event_capacity;

    uint8_t tx_seq;
    unsigned int tx_frames;
    unsigned int rx_frames;
    char last_rx_status[64];
    char rx_user[64];

    int ack_enabled;
    int ack_timeout_ms;
    int ack_retries;
    int ack_waiting;
    uint8_t ack_wait_seq;
    int ack_result;
    unsigned int ack_sent;
    unsigned int nack_sent;
    unsigned int ack_timeouts;
    unsigned int tx_retries;

    uint8_t rx_expected_seq;
    uint8_t rx_last_delivered;
    int rx_has_last;
};

static int node_is_running(phy_node_t *node)
{
    int running;

    pthread_mutex_lock(&node->lock);
    running = node->running;
    pthread_mutex_unlock(&node->lock);
    return running;
}

static void node_add_event(phy_node_t *node, const char *type, const char *format, ...)
{
    char detail[1024];
    char line[1200];
    char *copy;
    size_t id;
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(detail, sizeof(detail), format, arguments);
    va_end(arguments);

    pthread_mutex_lock(&node->lock);
    id = node->event_count;
    if (node->event_count == node->event_capacity) {
        size_t capacity = node->event_capacity == 0 ? 64u : node->event_capacity * 2u;
        char **grown = (char **)realloc(node->events, capacity * sizeof(char *));

        if (grown == NULL) {
            pthread_mutex_unlock(&node->lock);
            return;
        }
        node->events = grown;
        node->event_capacity = capacity;
    }
    snprintf(line, sizeof(line), "%s %zu %s", type, id, detail);
    copy = strdup(line);
    if (copy == NULL) {
        pthread_mutex_unlock(&node->lock);
        return;
    }
    node->events[node->event_count++] = copy;
    pthread_mutex_unlock(&node->lock);

    if (node->trace) {
        fprintf(stderr, "[%s] %s\n", node->name, line);
        fflush(stderr);
    }
}

static int node_send_bits(phy_node_t *node, const uint8_t *bits, size_t length)
{
    int result;

    pthread_mutex_lock(&node->tx_lock);
    result = phy_channel_send_bits(node->tx, bits, length);
    pthread_mutex_unlock(&node->tx_lock);
    return result;
}

static int node_send_control(phy_node_t *node, uint8_t control, uint8_t seq)
{
    phy_frame_t frame;
    uint8_t bits[PHY_FRAME_BITS];

    if (!node->ack_enabled) {
        return 0;
    }
    phy_frame_init(&frame, control, seq, 0, node->crc);
    phy_frame_to_bits(&frame, bits);
    if (!node_send_bits(node, bits, PHY_FRAME_BITS)) {
        node_add_event(node, "ERROR", "TX %s seq=%u failed",
                       phy_control_name(control), seq);
        return 0;
    }

    pthread_mutex_lock(&node->lock);
    if (control == PHY_CONTROL_ACK) {
        node->ack_sent++;
    } else {
        node->nack_sent++;
    }
    pthread_mutex_unlock(&node->lock);
    node_add_event(node, "TX", "%s seq=%u", phy_control_name(control), seq);
    return 1;
}

static void node_handle_ack(phy_node_t *node, uint8_t control, uint8_t seq)
{
    int matched = 0;

    pthread_mutex_lock(&node->lock);
    if (node->ack_waiting && node->ack_wait_seq == seq) {
        node->ack_result = control == PHY_CONTROL_ACK ? 1 : -1;
        node->ack_waiting = 0;
        matched = 1;
        pthread_cond_broadcast(&node->ack_cond);
    }
    pthread_mutex_unlock(&node->lock);

    if (matched) {
        node_add_event(node, control == PHY_CONTROL_ACK ? "ACK" : "NACK",
                       "rx seq=%u", seq);
    }
}

static int node_wait_ack(phy_node_t *node, uint8_t seq)
{
    struct timespec deadline;
    int result = 0;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += node->ack_timeout_ms / 1000;
    deadline.tv_nsec += (long)(node->ack_timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&node->lock);
    while (node->running && node->ack_waiting) {
        int wait_result = pthread_cond_timedwait(&node->ack_cond, &node->lock,
                                                 &deadline);
        if (wait_result != 0) {
            break;
        }
    }
    if (!node->ack_waiting) {
        result = node->ack_result;
    }
    node->ack_waiting = 0;
    if (result == 0 && node->running) {
        node->ack_timeouts++;
    }
    pthread_mutex_unlock(&node->lock);

    if (result == 0) {
        node_add_event(node, "TIMEOUT", "seq=%u", seq);
    }
    return result;
}

static int node_send_frame_acked(phy_node_t *node, const phy_frame_t *frame,
                                 const uint8_t *bits)
{
    int attempt;

    for (attempt = 0; attempt < node->ack_retries; attempt++) {
        int ack_result;

        pthread_mutex_lock(&node->lock);
        node->ack_waiting = 1;
        node->ack_wait_seq = frame->seq;
        node->ack_result = 0;
        pthread_mutex_unlock(&node->lock);

        if (!node_send_bits(node, bits, PHY_FRAME_BITS)) {
            pthread_mutex_lock(&node->lock);
            node->ack_waiting = 0;
            pthread_mutex_unlock(&node->lock);
            node_add_event(node, "ERROR", "TX seq=%u failed", frame->seq);
            return 0;
        }
        node_add_event(node, "TX",
                       "control=%s seq=%u data=0x%02X crc=0x%02X attempt=%d",
                       phy_control_name(frame->control), (unsigned int)frame->seq,
                       (unsigned int)frame->data, (unsigned int)frame->crc,
                       attempt + 1);

        ack_result = node_wait_ack(node, frame->seq);
        if (ack_result == 1) {
            pthread_mutex_lock(&node->lock);
            node->tx_seq = (uint8_t)((frame->seq + 1u) & 0x0Fu);
            node->tx_frames++;
            pthread_mutex_unlock(&node->lock);
            return 1;
        }
        if (!node_is_running(node)) {
            return 0;
        }

        pthread_mutex_lock(&node->lock);
        node->tx_retries++;
        pthread_mutex_unlock(&node->lock);
    }

    node_add_event(node, "ERROR", "sin ACK seq=%u", (unsigned int)frame->seq);
    return 0;
}

static int node_enqueue(phy_node_t *node, int is_bits, const char *user,
                        const uint8_t *payload, size_t length, unsigned long *job_id)
{
    phy_node_job_t *job;

    if (node == NULL || payload == NULL || length == 0) {
        return 0;
    }
    job = (phy_node_job_t *)calloc(1, sizeof(*job));
    if (job == NULL) {
        return 0;
    }
    job->payload = (uint8_t *)malloc(length);
    if (job->payload == NULL) {
        free(job);
        return 0;
    }
    memcpy(job->payload, payload, length);
    job->length = length;
    job->is_bits = is_bits;
    if (user != NULL) {
        size_t user_length = strlen(user);

        if (user_length >= sizeof(job->user)) {
            user_length = sizeof(job->user) - 1u;
        }
        memcpy(job->user, user, user_length);
        job->user[user_length] = '\0';
    }

    pthread_mutex_lock(&node->lock);
    if (!node->running || node->job_count >= PHY_NODE_MAX_JOBS) {
        pthread_mutex_unlock(&node->lock);
        free(job->payload);
        free(job);
        return 0;
    }
    job->id = node->next_job_id++;
    if (node->job_tail == NULL) {
        node->job_head = job;
    } else {
        node->job_tail->next = job;
    }
    node->job_tail = job;
    node->job_count++;
    pthread_cond_signal(&node->tx_cond);
    pthread_mutex_unlock(&node->lock);

    if (job_id != NULL) {
        *job_id = job->id;
    }
    return 1;
}

static int send_line(int fd, const char *format, ...)
{
    char line[2048];
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (length < 0) {
        return 0;
    }
    if ((size_t)length >= sizeof(line)) {
        length = (int)sizeof(line) - 1;
    }
    return phy_net_send_all(fd, line, (size_t)length) &&
           phy_net_send_all(fd, "\n", 1);
}

static int parse_unsigned(const char *text, unsigned long *value)
{
    char *end = NULL;
    unsigned long parsed;

    if (text == NULL || *text == '\0' || value == NULL) {
        return 0;
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }
    *value = parsed;
    return 1;
}

static void handle_poll(phy_node_t *node, int fd, const char *argument)
{
    unsigned long since;
    size_t index;
    size_t total;
    size_t count;
    char **snapshot = NULL;

    if (!parse_unsigned(argument, &since)) {
        send_line(fd, "ERR POLL since");
        return;
    }

    pthread_mutex_lock(&node->lock);
    total = node->event_count;
    if ((size_t)since > total) {
        since = (unsigned long)total;
    }
    count = total - (size_t)since;
    if (count > 0) {
        size_t copied = 0;

        snapshot = (char **)calloc(count, sizeof(char *));
        if (snapshot != NULL) {
            for (index = 0; index < count; index++) {
                snapshot[index] = strdup(node->events[(size_t)since + index]);
                if (snapshot[index] == NULL) {
                    break;
                }
                copied++;
            }
        }
        if (snapshot == NULL || copied != count) {
            for (index = 0; index < copied; index++) {
                free(snapshot[index]);
            }
            free(snapshot);
            snapshot = NULL;
            pthread_mutex_unlock(&node->lock);
            send_line(fd, "ERR memory");
            send_line(fd, "NEXT %zu", total);
            return;
        }
    }
    pthread_mutex_unlock(&node->lock);

    for (index = 0; index < count; index++) {
        phy_net_send_all(fd, snapshot[index], strlen(snapshot[index]));
        phy_net_send_all(fd, "\n", 1);
        free(snapshot[index]);
    }
    free(snapshot);
    send_line(fd, "NEXT %zu", total);
}

static void handle_status(phy_node_t *node, int fd)
{
    size_t queued;
    size_t events;
    unsigned int tx_frames;
    unsigned int rx_frames;
    unsigned int ack_sent;
    unsigned int nack_sent;
    unsigned int ack_timeouts;
    unsigned int tx_retries;
    char last_rx[64];
    int running;

    pthread_mutex_lock(&node->lock);
    queued = node->job_count;
    events = node->event_count;
    tx_frames = node->tx_frames;
    rx_frames = node->rx_frames;
    ack_sent = node->ack_sent;
    nack_sent = node->nack_sent;
    ack_timeouts = node->ack_timeouts;
    tx_retries = node->tx_retries;
    memcpy(last_rx, node->last_rx_status, sizeof(last_rx));
    running = node->running;
    pthread_mutex_unlock(&node->lock);

    send_line(fd, "STATUS node=%s running=%d queued=%zu events=%zu tx_frames=%u rx_frames=%u ack=%u nack=%u timeouts=%u retries=%u last_rx=%s",
              node->name, running, queued, events, tx_frames, rx_frames,
              ack_sent, nack_sent, ack_timeouts, tx_retries,
              last_rx[0] == '\0' ? "none" : last_rx);
}

static int handle_send(phy_node_t *node, int fd, const char *argument)
{
    const char *separator;
    const char *text;
    char user[64];
    unsigned long job_id = 0;
    size_t user_length;
    size_t length;

    separator = strchr(argument, ' ');
    if (separator == NULL || separator == argument || separator[1] == '\0') {
        send_line(fd, "ERR SEND user text");
        return 0;
    }
    user_length = (size_t)(separator - argument);
    if (user_length >= sizeof(user)) {
        send_line(fd, "ERR SEND user too long");
        return 0;
    }
    memcpy(user, argument, user_length);
    user[user_length] = '\0';
    text = separator + 1;
    length = strlen(text);
    if (length > PHY_NODE_MAX_TEXT) {
        send_line(fd, "ERR SEND too long");
        return 0;
    }
    if (!node_enqueue(node, 0, user, (const uint8_t *)text, length, &job_id)) {
        send_line(fd, "ERR SEND queue");
        return 0;
    }
    send_line(fd, "OK SEND %lu", job_id);
    return 1;
}

static int handle_bits(phy_node_t *node, int fd, const char *argument)
{
    uint8_t bits[PHY_NODE_MAX_BITS];
    size_t length;
    size_t index;
    unsigned long job_id = 0;

    length = strlen(argument);
    if (length == 0 || length > sizeof(bits)) {
        send_line(fd, "ERR BITS length");
        return 0;
    }
    for (index = 0; index < length; index++) {
        if (argument[index] != '0' && argument[index] != '1') {
            send_line(fd, "ERR BITS digits");
            return 0;
        }
        bits[index] = (uint8_t)(argument[index] - '0');
    }
    if (!node_enqueue(node, 1, NULL, bits, length, &job_id)) {
        send_line(fd, "ERR BITS queue");
        return 0;
    }
    send_line(fd, "OK BITS %zu", length);
    return 1;
}

static void handle_command(phy_node_t *node, int fd, const char *line,
                           char *connection_user, size_t user_capacity, int *quit)
{
    if (strcmp(line, "PING") == 0) {
        send_line(fd, "PONG");
        return;
    }
    if (strcmp(line, "STATUS") == 0) {
        handle_status(node, fd);
        return;
    }
    if (strcmp(line, "QUIT") == 0) {
        send_line(fd, "BYE");
        *quit = 1;
        return;
    }
    if (strncmp(line, "HELLO ", 6) == 0) {
        const char *user = line + 6;
        size_t user_length;

        if (*user == '\0') {
            send_line(fd, "ERR HELLO user");
            return;
        }
        user_length = strlen(user);
        if (user_length >= user_capacity) {
            user_length = user_capacity - 1u;
        }
        memcpy(connection_user, user, user_length);
        connection_user[user_length] = '\0';
        send_line(fd, "OK HELLO %s", connection_user);
        return;
    }
    if (strncmp(line, "SEND ", 5) == 0) {
        handle_send(node, fd, line + 5);
        return;
    }
    if (strncmp(line, "BITS ", 5) == 0) {
        handle_bits(node, fd, line + 5);
        return;
    }
    if (strncmp(line, "POLL ", 5) == 0) {
        handle_poll(node, fd, line + 5);
        return;
    }
    send_line(fd, "ERR unknown");
}

static void handle_api_connection(phy_node_t *node, int fd)
{
    phy_net_reader_t reader;
    char line[512];
    char connection_user[64];

    phy_net_reader_init(&reader, fd);
    snprintf(connection_user, sizeof(connection_user), "anon");

    while (node_is_running(node)) {
        int result = phy_net_read_line(&reader, line, sizeof(line), 200);
        size_t length;

        if (result < 0) {
            break;
        }
        if (result == 0) {
            continue;
        }
        length = strlen(line);
        while (length > 0 && (line[length - 1] == '\r' || line[length - 1] == ' ')) {
            line[--length] = '\0';
        }
        if (length == 0) {
            continue;
        }
        {
            int quit = 0;

            handle_command(node, fd, line, connection_user,
                           sizeof(connection_user), &quit);
            if (quit) {
                break;
            }
        }
    }
}

static void *api_main(void *argument)
{
    phy_node_t *node = (phy_node_t *)argument;

    while (node_is_running(node)) {
        struct pollfd descriptor;
        int result;
        int fd;

        descriptor.fd = node->listen_fd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        result = poll(&descriptor, 1, 100);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (result == 0) {
            continue;
        }
        fd = phy_net_accept(node->listen_fd);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        handle_api_connection(node, fd);
        close(fd);
    }
    return NULL;
}

static void rx_flush_message(phy_node_t *node, uint8_t *message, size_t *length)
{
    message[*length] = '\0';
    node_add_event(node, "MESSAGE", "%s %s", node->rx_user, (char *)message);
    *length = 0;
}

static void rx_set_user(phy_node_t *node, uint8_t *buffer, size_t length)
{
    buffer[length] = '\0';
    if (length == 0) {
        memcpy(node->rx_user, "anon", 5);
        return;
    }
    if (length >= sizeof(node->rx_user)) {
        length = sizeof(node->rx_user) - 1u;
    }
    memcpy(node->rx_user, buffer, length);
    node->rx_user[length] = '\0';
}

static void *rx_main(void *argument)
{
    phy_node_t *node = (phy_node_t *)argument;
    uint8_t message[PHY_NODE_RX_MESSAGE];
    size_t message_length = 0;
    uint8_t hello[80];
    size_t hello_length = 0;

    while (node_is_running(node)) {
        phy_frame_t frame;
        phy_frame_status_t status;
        uint8_t wire[PHY_FRAME_BITS];
        int result;

        result = phy_rx_frame(node->rx, node->crc, &frame, &status,
                              wire, sizeof(wire), node->rx_timeout_ms);
        if (result < 0) {
            break;
        }
        if (result == 0) {
            continue;
        }

        pthread_mutex_lock(&node->lock);
        node->rx_frames++;
        snprintf(node->last_rx_status, sizeof(node->last_rx_status), "%s",
                 phy_frame_status_text(status));
        pthread_mutex_unlock(&node->lock);

        node_add_event(node, "RX", "control=%s seq=%u data=0x%02X crc=0x%02X status=%s",
                       phy_control_name(frame.control), (unsigned int)frame.seq,
                       (unsigned int)frame.data, (unsigned int)frame.crc,
                       phy_frame_status_text(status));

        if (status != PHY_FRAME_OK) {
            if (node->ack_enabled && status == PHY_FRAME_ERR_CRC) {
                node_send_control(node, PHY_CONTROL_NACK, frame.seq);
            }
            continue;
        }

        if (frame.control == PHY_CONTROL_ACK || frame.control == PHY_CONTROL_NACK) {
            node_handle_ack(node, frame.control, frame.seq);
            continue;
        }

        if (frame.control != PHY_CONTROL_DATA &&
            frame.control != PHY_CONTROL_HELLO) {
            continue;
        }

        {
            int deliver = 0;
            int duplicate = 0;

            pthread_mutex_lock(&node->lock);
            if (!node->rx_has_last) {
                node->rx_has_last = 1;
                node->rx_last_delivered = frame.seq;
                node->rx_expected_seq = (uint8_t)((frame.seq + 1u) & 0x0Fu);
                deliver = 1;
            } else if (frame.seq == node->rx_expected_seq) {
                node->rx_last_delivered = frame.seq;
                node->rx_expected_seq = (uint8_t)((frame.seq + 1u) & 0x0Fu);
                deliver = 1;
            } else if (frame.seq == node->rx_last_delivered) {
                duplicate = 1;
            }
            pthread_mutex_unlock(&node->lock);

            if (duplicate) {
                node_send_control(node, PHY_CONTROL_ACK, frame.seq);
                continue;
            }
            if (!deliver) {
                node_send_control(node, PHY_CONTROL_NACK, frame.seq);
                continue;
            }
        }
        {
            uint8_t decoded[8];
            size_t decoded_length = 0;
            size_t index;

            if (!phy_decode_output(&frame.data, 1, node->encoding, decoded,
                                   sizeof(decoded), &decoded_length)) {
                node_add_event(node, "ERROR", "decode byte 0x%02X",
                               (unsigned int)frame.data);
                node_send_control(node, PHY_CONTROL_NACK, frame.seq);
                continue;
            }
            for (index = 0; index < decoded_length; index++) {
                if (frame.control == PHY_CONTROL_HELLO) {
                    if (decoded[index] == '\n') {
                        rx_set_user(node, hello, hello_length);
                        hello_length = 0;
                    } else if (hello_length + 1u < sizeof(hello)) {
                        hello[hello_length++] = decoded[index];
                    } else {
                        hello_length = 0;
                        node_add_event(node, "ERROR", "hello overflow");
                    }
                } else if (decoded[index] == '\n') {
                    rx_flush_message(node, message, &message_length);
                } else if (message_length + 1u < sizeof(message)) {
                    message[message_length++] = decoded[index];
                } else {
                    message_length = 0;
                    node_add_event(node, "ERROR", "message overflow");
                }
            }
        }
        node_send_control(node, PHY_CONTROL_ACK, frame.seq);
    }
    return NULL;
}

static int tx_emit_frames(phy_node_t *node, uint8_t control,
                          const uint8_t *payload, size_t length)
{
    size_t index;

    for (index = 0; index < length; index++) {
        phy_frame_t frame;
        uint8_t bits[PHY_FRAME_BITS];
        uint8_t seq;

        if (!node_is_running(node)) {
            return 0;
        }
        pthread_mutex_lock(&node->lock);
        seq = node->tx_seq;
        pthread_mutex_unlock(&node->lock);
        phy_frame_init(&frame, control, seq, payload[index], node->crc);
        phy_frame_to_bits(&frame, bits);

        if (node->ack_enabled) {
            if (!node_send_frame_acked(node, &frame, bits)) {
                return 0;
            }
        } else {
            if (!node_send_bits(node, bits, PHY_FRAME_BITS)) {
                node_add_event(node, "ERROR", "TX frame %zu failed", index + 1u);
                return 0;
            }
            pthread_mutex_lock(&node->lock);
            node->tx_seq = (uint8_t)((node->tx_seq + 1u) & 0x0Fu);
            node->tx_frames++;
            pthread_mutex_unlock(&node->lock);
            node_add_event(node, "TX",
                           "frame=%zu/%zu control=%s seq=%u data=0x%02X crc=0x%02X",
                           index + 1u, length, phy_control_name(frame.control),
                           (unsigned int)frame.seq, (unsigned int)frame.data,
                           (unsigned int)frame.crc);
        }
    }
    return 1;
}

static void *tx_main(void *argument)
{
    phy_node_t *node = (phy_node_t *)argument;

    for (;;) {
        phy_node_job_t *job;

        pthread_mutex_lock(&node->lock);
        while (!node->running && node->job_head == NULL) {
            pthread_mutex_unlock(&node->lock);
            return NULL;
        }
        while (node->running && node->job_head == NULL) {
            pthread_cond_wait(&node->tx_cond, &node->lock);
        }
        if (!node->running) {
            pthread_mutex_unlock(&node->lock);
            return NULL;
        }
        job = node->job_head;
        node->job_head = job->next;
        if (node->job_head == NULL) {
            node->job_tail = NULL;
        }
        node->job_count--;
        pthread_mutex_unlock(&node->lock);

        if (job->is_bits) {
            if (node_send_bits(node, job->payload, job->length)) {
                node_add_event(node, "TX", "BITS bits=%zu", job->length);
            } else {
                node_add_event(node, "ERROR", "TX BITS failed");
            }
        } else {
            uint8_t payload[PHY_NODE_MAX_TEXT + 2u];
            size_t payload_length = 0;
            int ready = 1;

            if (job->user[0] != '\0') {
                uint8_t user_payload[sizeof(job->user) + 2u];
                size_t user_length = 0;

                if (!phy_encode_line((const uint8_t *)job->user, strlen(job->user),
                                     node->encoding, user_payload,
                                     sizeof(user_payload), &user_length)) {
                    node_add_event(node, "ERROR", "encode user");
                    ready = 0;
                } else if (!tx_emit_frames(node, PHY_CONTROL_HELLO,
                                           user_payload, user_length)) {
                    ready = 0;
                }
            }
            if (ready && !phy_encode_line(job->payload, job->length, node->encoding,
                                          payload, sizeof(payload), &payload_length)) {
                node_add_event(node, "ERROR", "encode message");
                ready = 0;
            }
            if (ready && !tx_emit_frames(node, PHY_CONTROL_DATA, payload,
                                         payload_length)) {
                node_add_event(node, "ERROR", "mensaje abortado");
            }
        }
        free(job->payload);
        free(job);
    }
}

static int socket_port(int fd)
{
    struct sockaddr_storage address;
    socklen_t length = sizeof(address);
    int port = -1;

    if (getsockname(fd, (struct sockaddr *)&address, &length) != 0) {
        return -1;
    }
    if (address.ss_family == AF_INET) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)&address;

        port = (int)ntohs(ipv4->sin_port);
    } else if (address.ss_family == AF_INET6) {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&address;

        port = (int)ntohs(ipv6->sin6_port);
    }
    return port;
}

static void node_destroy(phy_node_t *node)
{
    phy_node_job_t *job;

    if (node->listen_fd >= 0) {
        close(node->listen_fd);
    }
    job = node->job_head;
    while (job != NULL) {
        phy_node_job_t *next = job->next;

        free(job->payload);
        free(job);
        job = next;
    }
    while (node->event_count > 0) {
        free(node->events[--node->event_count]);
    }
    free(node->events);
    pthread_cond_destroy(&node->ack_cond);
    pthread_mutex_destroy(&node->tx_lock);
    pthread_cond_destroy(&node->tx_cond);
    pthread_mutex_destroy(&node->lock);
    free(node->name);
    free(node->bind_host);
    free(node);
}

int phy_node_start(phy_node_t **out, const phy_node_config_t *config)
{
    phy_node_t *node;

    if (out == NULL || config == NULL || config->tx == NULL || config->rx == NULL) {
        return 0;
    }
    node = (phy_node_t *)calloc(1, sizeof(*node));
    if (node == NULL) {
        return 0;
    }
    node->listen_fd = -1;
    node->name = strdup(config->name != NULL && *config->name != '\0' ? config->name : "node");
    node->bind_host = strdup(config->bind_host != NULL ? config->bind_host : "127.0.0.1");
    if (node->name == NULL || node->bind_host == NULL) {
        free(node->name);
        free(node->bind_host);
        free(node);
        return 0;
    }
    node->tx = config->tx;
    node->rx = config->rx;
    node->crc = config->crc;
    node->encoding = config->encoding;
    node->rx_timeout_ms = config->rx_timeout_ms > 0 ? config->rx_timeout_ms : 200;
    node->trace = config->trace;
    node->ack_enabled = config->ack_enabled ? 1 : 0;
    node->ack_timeout_ms = config->ack_timeout_ms > 0 ? config->ack_timeout_ms : 6000;
    node->ack_retries = config->ack_retries > 0 ? config->ack_retries : 3;
    node->rx_expected_seq = 0;
    node->rx_last_delivered = 0;
    node->rx_has_last = 0;
    snprintf(node->rx_user, sizeof(node->rx_user), "anon");

    if (pthread_mutex_init(&node->lock, NULL) != 0) {
        free(node->name);
        free(node->bind_host);
        free(node);
        return 0;
    }
    if (pthread_cond_init(&node->tx_cond, NULL) != 0) {
        pthread_mutex_destroy(&node->lock);
        free(node->name);
        free(node->bind_host);
        free(node);
        return 0;
    }
    if (pthread_mutex_init(&node->tx_lock, NULL) != 0) {
        pthread_cond_destroy(&node->tx_cond);
        pthread_mutex_destroy(&node->lock);
        free(node->name);
        free(node->bind_host);
        free(node);
        return 0;
    }
    if (pthread_cond_init(&node->ack_cond, NULL) != 0) {
        pthread_mutex_destroy(&node->tx_lock);
        pthread_cond_destroy(&node->tx_cond);
        pthread_mutex_destroy(&node->lock);
        free(node->name);
        free(node->bind_host);
        free(node);
        return 0;
    }

    node->listen_fd = phy_net_listen(node->bind_host, config->port);
    if (node->listen_fd < 0) {
        node_destroy(node);
        return 0;
    }
    node->port = socket_port(node->listen_fd);
    node->running = 1;

    if (pthread_create(&node->tx_thread, NULL, tx_main, node) != 0) {
        node_destroy(node);
        return 0;
    }
    node->tx_started = 1;
    if (pthread_create(&node->rx_thread, NULL, rx_main, node) != 0) {
        pthread_mutex_lock(&node->lock);
        node->running = 0;
        pthread_cond_broadcast(&node->tx_cond);
        pthread_cond_broadcast(&node->ack_cond);
        pthread_mutex_unlock(&node->lock);
        pthread_join(node->tx_thread, NULL);
        node_destroy(node);
        return 0;
    }
    node->rx_started = 1;
    if (pthread_create(&node->api_thread, NULL, api_main, node) != 0) {
        pthread_mutex_lock(&node->lock);
        node->running = 0;
        pthread_cond_broadcast(&node->tx_cond);
        pthread_cond_broadcast(&node->ack_cond);
        pthread_mutex_unlock(&node->lock);
        pthread_join(node->rx_thread, NULL);
        pthread_join(node->tx_thread, NULL);
        node_destroy(node);
        return 0;
    }
    node->api_started = 1;

    *out = node;
    return 1;
}

void phy_node_stop(phy_node_t *node)
{
    if (node == NULL) {
        return;
    }
    pthread_mutex_lock(&node->lock);
    node->running = 0;
    pthread_cond_broadcast(&node->tx_cond);
    pthread_cond_broadcast(&node->ack_cond);
    pthread_mutex_unlock(&node->lock);

    if (node->api_started) {
        pthread_join(node->api_thread, NULL);
        node->api_started = 0;
    }
    if (node->rx_started) {
        pthread_join(node->rx_thread, NULL);
        node->rx_started = 0;
    }
    if (node->tx_started) {
        pthread_join(node->tx_thread, NULL);
        node->tx_started = 0;
    }
    node_destroy(node);
}

int phy_node_port(const phy_node_t *node)
{
    return node == NULL ? -1 : node->port;
}
