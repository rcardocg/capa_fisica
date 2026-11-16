#define _POSIX_C_SOURCE 200809L

#include "phy_net.h"
#include "phy_protocol.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int connect_mode;
    const char *host;
    const char *bind_address;
    int port;
    phy_encoding_t encoding;
    phy_crc_algorithm_t crc;
    int lanes;
    int timeout_ms;
    int retries;
    int corrupt_frame;
    int corrupt_bit;
    int seq_start;
    int trace_enabled;
    int show_help;
} options_t;

static int trace_enabled = 1;

static void trace_log(const char *tag, const char *format, ...)
{
    va_list arguments;

    if (!trace_enabled) {
        return;
    }
    fprintf(stderr, "[%s] ", tag);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    fflush(stderr);
}

static uint32_t bits_get(const uint8_t *bits, size_t offset, unsigned int width)
{
    uint32_t value = 0;
    unsigned int i;

    for (i = 0; i < width; i++) {
        value = (value << 1) | (uint32_t)(bits[offset + i] & 1u);
    }
    return value;
}

static size_t utf8_character_count(const uint8_t *input, size_t length)
{
    size_t count = 0;
    size_t i;

    for (i = 0; i < length; i++) {
        if ((input[i] & 0xC0u) != 0x80u) {
            count++;
        }
    }
    return count;
}

static void trace_frame(const char *tag, const char *direction, const uint8_t *wire, size_t wire_length, phy_crc_algorithm_t algorithm, int corrupted)
{
    phy_frame_t frame;
    uint8_t calculated;
    char wire_text[PHY_MAX_WIRE_BITS + 1u];
    size_t i;

    if (!trace_enabled) {
        return;
    }
    if (wire == NULL || wire_length < PHY_FRAME_BITS) {
        trace_log(tag, "%s TRAMA INCOMPLETA (%zu bits)", direction, wire_length);
        return;
    }

    frame.control = (uint8_t)bits_get(wire, 20, PHY_CONTROL_BITS);
    frame.seq = (uint8_t)bits_get(wire, 24, PHY_SEQ_BITS);
    frame.data = (uint8_t)bits_get(wire, 28, PHY_DATA_BITS);
    frame.crc = (uint8_t)bits_get(wire, 36, PHY_CRC_BITS);
    frame.algorithm = algorithm;
    calculated = phy_frame_crc(&frame);

    trace_log(tag,
              "%s SYN=0x%04X SFD=0x%X CONTROL=0x%X(%s) SEQ=%u DATA=0x%02X '%c' CRC=0x%02X CALC=0x%02X%s",
              direction,
              (unsigned int)bits_get(wire, 0, PHY_SYN_BITS),
              (unsigned int)bits_get(wire, 16, PHY_SFD_BITS),
              (unsigned int)frame.control,
              phy_control_name(frame.control),
              (unsigned int)frame.seq,
              (unsigned int)frame.data,
              (frame.data >= 32u && frame.data < 127u) ? (char)frame.data : '.',
              (unsigned int)frame.crc,
              (unsigned int)calculated,
              corrupted ? " CORRUPTO" : "");

    for (i = 0; i < wire_length && i < PHY_MAX_WIRE_BITS; i++) {
        wire_text[i] = wire[i] ? '1' : '0';
    }
    wire_text[i] = '\0';
    trace_log(tag, "%s WIRE (%zu bits)=%s", direction, wire_length, wire_text);
}

static int parse_int(const char *text, int *out)
{
    char *end = NULL;
    long value;

    if (text == NULL || out == NULL || *text == '\0') {
        return 0;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < INT_MIN || value > INT_MAX) {
        return 0;
    }
    *out = (int)value;
    return 1;
}

static int parse_timeout_ms(const char *text, int *out)
{
    char *end = NULL;
    double seconds;

    if (text == NULL || out == NULL || *text == '\0') {
        return 0;
    }
    errno = 0;
    seconds = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || seconds <= 0.0 || seconds > 3600.0) {
        return 0;
    }
    *out = (int)(seconds * 1000.0 + 0.5);
    return 1;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Uso:\n"
            "  %s connect [opciones]    emisor interactivo\n"
            "  %s serve [opciones]      receptor interactivo\n"
            "\n"
            "Opciones:\n"
            "  --host DIRECCION         destino del emisor (defecto 127.0.0.1)\n"
            "  --bind DIRECCION         interfaz del receptor (defecto 127.0.0.1)\n"
            "  --port PUERTO            puerto TCP (defecto 5000)\n"
            "  --encoding utf-8|latin-1 codificacion (defecto utf-8)\n"
            "  --crc NOMBRE             smbus|maxim-dow|sae-j1850|autosar\n"
            "  --lanes N                carriles 1..7 (defecto 3)\n"
            "  --timeout SEGUNDOS       espera de ACK/NACK (defecto 1.0)\n"
            "  --retries N              intentos por trama (defecto 3)\n"
            "  --seq-start N            secuencia inicial 0..15 (defecto 0)\n"
            "  --corrupt-frame N        corrompe la trama N en el primer intento\n"
            "  --corrupt-bit N          bit a invertir (defecto 43)\n"
            "  --quiet                  desactiva las trazas\n"
            "  --help                   muestra esta ayuda\n",
            program, program);
}

static int parse_options(int argc, char **argv, options_t *options)
{
    static const struct option long_options[] = {
        {"host", required_argument, NULL, 'H'},
        {"bind", required_argument, NULL, 'B'},
        {"port", required_argument, NULL, 'p'},
        {"encoding", required_argument, NULL, 'e'},
        {"crc", required_argument, NULL, 'c'},
        {"lanes", required_argument, NULL, 'l'},
        {"timeout", required_argument, NULL, 't'},
        {"retries", required_argument, NULL, 'r'},
        {"seq-start", required_argument, NULL, 's'},
        {"corrupt-frame", required_argument, NULL, 'f'},
        {"corrupt-bit", required_argument, NULL, 'b'},
        {"quiet", no_argument, NULL, 'q'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int option;

    options->connect_mode = 0;
    options->host = "127.0.0.1";
    options->bind_address = "127.0.0.1";
    options->port = 5000;
    options->encoding = PHY_ENCODING_UTF8;
    options->crc = PHY_CRC_SMBUS;
    options->lanes = 3;
    options->timeout_ms = 1000;
    options->retries = 3;
    options->corrupt_frame = -1;
    options->corrupt_bit = 43;
    options->seq_start = 0;
    options->trace_enabled = 1;
    options->show_help = 0;

    if (argc < 2) {
        return 0;
    }
    if (strcmp(argv[1], "connect") == 0 || strcmp(argv[1], "emisor") == 0) {
        options->connect_mode = 1;
    } else if (strcmp(argv[1], "serve") == 0 || strcmp(argv[1], "receptor") == 0) {
        options->connect_mode = 0;
    } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        options->show_help = 1;
        return 1;
    } else {
        return 0;
    }

    opterr = 0;
    optind = 2;
    while ((option = getopt_long(argc, argv, "H:B:p:e:c:l:t:r:s:f:b:qh", long_options, NULL)) != -1) {
        switch (option) {
        case 'H':
            options->host = optarg;
            break;
        case 'B':
            options->bind_address = optarg;
            break;
        case 'p':
            if (!parse_int(optarg, &options->port)) {
                return 0;
            }
            break;
        case 'e':
            if (!phy_encoding_parse(optarg, &options->encoding)) {
                return 0;
            }
            break;
        case 'c':
            if (!phy_crc_parse(optarg, &options->crc)) {
                return 0;
            }
            break;
        case 'l':
            if (!parse_int(optarg, &options->lanes)) {
                return 0;
            }
            break;
        case 't':
            if (!parse_timeout_ms(optarg, &options->timeout_ms)) {
                return 0;
            }
            break;
        case 'r':
            if (!parse_int(optarg, &options->retries)) {
                return 0;
            }
            break;
        case 's':
            if (!parse_int(optarg, &options->seq_start)) {
                return 0;
            }
            break;
        case 'f':
            if (!parse_int(optarg, &options->corrupt_frame)) {
                return 0;
            }
            break;
        case 'b':
            if (!parse_int(optarg, &options->corrupt_bit)) {
                return 0;
            }
            break;
        case 'q':
            options->trace_enabled = 0;
            break;
        case 'h':
            options->show_help = 1;
            return 1;
        default:
            return 0;
        }
    }

    if (options->port < 1 || options->port > 65535) {
        return 0;
    }
    if (options->lanes < 1 || options->lanes > PHY_MAX_LANES) {
        return 0;
    }
    if (options->retries < 1 || options->retries > 100) {
        return 0;
    }
    if (options->seq_start < 0 || options->seq_start > 15) {
        return 0;
    }
    if (options->corrupt_frame < -1) {
        return 0;
    }
    if (options->corrupt_bit < 0 || options->corrupt_bit >= PHY_MAX_WIRE_BITS) {
        return 0;
    }
    return 1;
}

static long now_ms(void)
{
    struct timespec timestamp;

    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (long)timestamp.tv_sec * 1000L + (long)(timestamp.tv_nsec / 1000000L);
}

static int send_byte_with_ack(int fd, phy_net_reader_t *reader, const options_t *options,
                              int seq, uint8_t value, int frame_index)
{
    int attempt;

    for (attempt = 0; attempt < options->retries; attempt++) {
        phy_frame_t frame;
        uint8_t wire[PHY_MAX_WIRE_BITS];
        size_t wire_length = 0;
        int corrupted = 0;
        phy_frame_t response;
        phy_frame_status_t status;
        uint8_t response_wire[PHY_MAX_WIRE_BITS];
        size_t response_wire_length = 0;
        long deadline;

        phy_frame_init(&frame, PHY_CONTROL_DATA, (uint8_t)seq, value, options->crc);
        phy_frame_to_wire_bits(&frame, options->lanes, wire, &wire_length);
        if (frame_index == options->corrupt_frame && attempt == 0 && options->corrupt_bit < (int)wire_length) {
            wire[options->corrupt_bit] ^= 1u;
            corrupted = 1;
        }

        trace_frame("EMISOR", "TX", wire, wire_length, options->crc, corrupted);
        if (!phy_net_send_wire(fd, options->lanes, wire, wire_length)) {
            trace_log("EMISOR", "ERROR enviando trama");
            return -1;
        }

        deadline = now_ms() + options->timeout_ms;
        for (;;) {
            int remaining = (int)(deadline - now_ms());
            int result;

            if (remaining <= 0) {
                trace_log("EMISOR", "TIMEOUT esperando ACK/NACK SEQ=%d", seq);
                break;
            }
            result = phy_net_recv_frame(reader, options->lanes, options->crc, &response, &status,
                                        response_wire, sizeof(response_wire), &response_wire_length,
                                        remaining);
            if (result < 0) {
                trace_log("EMISOR", "CONEXION CERRADA esperando respuesta");
                return -1;
            }
            if (result == 0) {
                trace_log("EMISOR", "TIMEOUT esperando ACK/NACK SEQ=%d", seq);
                break;
            }
            if (response_wire_length > 0) {
                trace_frame("EMISOR", "RX", response_wire, response_wire_length, options->crc, 0);
            }
            if (status != PHY_FRAME_OK) {
                trace_log("EMISOR", "RESPUESTA INVALIDA: %s", phy_frame_status_text(status));
                break;
            }
            if (response.control == PHY_CONTROL_ACK) {
                if (response.seq == seq) {
                    trace_log("EMISOR", "ACK SEQ=%d OK", seq);
                    return 1;
                }
                trace_log("EMISOR", "ACK obsoleto SEQ=%u, se ignora", response.seq);
                continue;
            }
            if (response.control == PHY_CONTROL_NACK) {
                if (response.seq == seq) {
                    trace_log("EMISOR", "NACK recibido para SEQ=%d", seq);
                } else {
                    trace_log("EMISOR", "NACK obsoleto SEQ=%u, se ignora", response.seq);
                    continue;
                }
                break;
            }
            trace_log("EMISOR", "RESPUESTA inesperada CONTROL=0x%X SEQ=%u",
                      response.control, response.seq);
            break;
        }

        if (attempt + 1 < options->retries) {
            trace_log("EMISOR", "REINTENTO %d/%d para SEQ=%d", attempt + 1, options->retries, seq);
        }
    }

    trace_log("EMISOR", "FALLO definitivo para SEQ=%d tras %d intentos", seq, options->retries);
    return 0;
}

static int run_connect(const options_t *options)
{
    int fd;
    phy_net_reader_t reader;
    int seq = options->seq_start;
    int frame_index = 0;
    char *line = NULL;
    size_t capacity = 0;

    fd = phy_net_connect(options->host, options->port);
    if (fd < 0) {
        trace_log("EMISOR", "No se pudo conectar a %s:%d", options->host, options->port);
        return 1;
    }
    phy_net_reader_init(&reader, fd);
    trace_log("EMISOR", "Conectado a %s:%d encoding=%s crc=%s lanes=%d",
              options->host, options->port, phy_encoding_name(options->encoding),
              phy_crc_name(options->crc), options->lanes);

    for (;;) {
        ssize_t length;
        uint8_t payload[8192];
        size_t payload_length = 0;
        size_t i;
        int failed = 0;

        fprintf(stderr, "[EMISOR] mensaje> ");
        fflush(stderr);
        length = getline(&line, &capacity, stdin);
        if (length < 0) {
            break;
        }
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (!phy_encode_line((const uint8_t *)line, (size_t)length, options->encoding,
                             payload, sizeof(payload), &payload_length)) {
            trace_log("EMISOR", "No se pudo codificar el mensaje como %s",
                      phy_encoding_name(options->encoding));
            continue;
        }

        trace_log("EMISOR", "MENSAJE '%s' caracteres=%zu bytes=%zu",
                  line, utf8_character_count((const uint8_t *)line, (size_t)length), payload_length);
        for (i = 0; i < payload_length; i++) {
            int result;

            trace_log("EMISOR", "BYTE %zu/%zu valor=0x%02X", i + 1u, payload_length,
                      (unsigned int)payload[i]);
            result = send_byte_with_ack(fd, &reader, options, seq, payload[i], frame_index);
            if (result <= 0) {
                failed = 1;
                break;
            }
            seq = (seq + 1) & 0x0F;
            frame_index++;
        }
        if (failed) {
            trace_log("EMISOR", "Mensaje abortado");
        }
    }

    free(line);
    close(fd);
    trace_log("EMISOR", "Conexion finalizada");
    return 0;
}

static int send_control(int fd, const options_t *options, uint8_t control, uint8_t seq)
{
    phy_frame_t frame;
    uint8_t wire[PHY_MAX_WIRE_BITS];
    size_t wire_length = 0;

    phy_frame_init(&frame, control, seq, 0, options->crc);
    phy_frame_to_wire_bits(&frame, options->lanes, wire, &wire_length);
    trace_frame("RECEPTOR", "TX", wire, wire_length, options->crc, 0);
    return phy_net_send_wire(fd, options->lanes, wire, wire_length);
}

static void append_message(uint8_t *message, size_t *message_length, const uint8_t *output, size_t output_length)
{
    if (*message_length + output_length > 8192u) {
        trace_log("RECEPTOR", "MENSAJE demasiado largo, se descarta");
        *message_length = 0;
        return;
    }
    memcpy(message + *message_length, output, output_length);
    *message_length += output_length;

    for (;;) {
        uint8_t *newline = (uint8_t *)memchr(message, '\n', *message_length);
        size_t line_length;
        size_t characters;

        if (newline == NULL) {
            break;
        }
        line_length = (size_t)(newline - message);
        characters = utf8_character_count(message, line_length);
        trace_log("RECEPTOR", "MENSAJE COMPLETO (%zu caracteres): '%.*s'",
                  characters, (int)line_length, (char *)message);
        memmove(message, newline + 1, *message_length - line_length - 1u);
        *message_length -= line_length + 1u;
    }
}

static void handle_connection(int fd, const options_t *options)
{
    phy_net_reader_t reader;
    int expected_seq = 0;
    int last_delivered = -1;
    uint8_t message[8192];
    size_t message_length = 0;

    phy_net_reader_init(&reader, fd);
    trace_log("RECEPTOR", "Cliente conectado");

    for (;;) {
        phy_frame_t frame;
        phy_frame_status_t status;
        uint8_t wire[PHY_MAX_WIRE_BITS];
        size_t wire_length = 0;
        int result;
        uint8_t seq = 0;

        result = phy_net_recv_frame(&reader, options->lanes, options->crc, &frame, &status,
                                    wire, sizeof(wire), &wire_length, -1);
        if (result <= 0) {
            break;
        }
        if (wire_length >= 28u) {
            seq = (uint8_t)bits_get(wire, 24, PHY_SEQ_BITS);
        }
        if (wire_length > 0) {
            trace_frame("RECEPTOR", "RX", wire, wire_length, options->crc, 0);
        } else {
            trace_log("RECEPTOR", "TRAMA mal formada");
            send_control(fd, options, PHY_CONTROL_NACK, seq);
            continue;
        }

        if (status != PHY_FRAME_OK) {
            trace_log("RECEPTOR", "FALLO EN TRAMA: %s -> NACK", phy_frame_status_text(status));
            send_control(fd, options, PHY_CONTROL_NACK, seq);
            continue;
        }
        trace_log("RECEPTOR", "CRC OK CONTROL=%s SEQ=%u DATA=0x%02X",
                  phy_control_name(frame.control), (unsigned int)frame.seq,
                  (unsigned int)frame.data);

        if (frame.control != PHY_CONTROL_DATA) {
            trace_log("RECEPTOR", "CONTROL=%s ignorado", phy_control_name(frame.control));
            continue;
        }

        if (frame.seq == (uint8_t)expected_seq) {
            uint8_t output[8];
            size_t output_length = 0;

            if (!phy_decode_output(&frame.data, 1, options->encoding, output, sizeof(output), &output_length)) {
                trace_log("RECEPTOR", "No se pudo decodificar el byte 0x%02X", (unsigned int)frame.data);
            } else {
                fwrite(output, 1, output_length, stdout);
                fflush(stdout);
                if (output_length == 1u && output[0] >= 32u && output[0] < 127u) {
                    trace_log("RECEPTOR", "TEXTO recibido='%c'", (char)output[0]);
                } else {
                    trace_log("RECEPTOR", "TEXTO recibido (%zu bytes)", output_length);
                }
                append_message(message, &message_length, output, output_length);
            }
            last_delivered = frame.seq;
            expected_seq = (expected_seq + 1) & 0x0F;
            send_control(fd, options, PHY_CONTROL_ACK, frame.seq);
            trace_log("RECEPTOR", "ACK SEQ=%u enviado", (unsigned int)frame.seq);
        } else if (frame.seq == (uint8_t)last_delivered) {
            trace_log("RECEPTOR", "DUPLICADO SEQ=%u, se reenvia ACK", (unsigned int)frame.seq);
            send_control(fd, options, PHY_CONTROL_ACK, frame.seq);
        } else {
            trace_log("RECEPTOR", "FUERA DE ORDEN SEQ=%u esperado=%d -> NACK",
                      (unsigned int)frame.seq, expected_seq);
            send_control(fd, options, PHY_CONTROL_NACK, frame.seq);
        }
    }

    if (message_length > 0) {
        trace_log("RECEPTOR", "MENSAJE INCOMPLETO (%zu bytes)", message_length);
    }
    trace_log("RECEPTOR", "Cliente desconectado");
}

static int run_serve(const options_t *options)
{
    int listen_fd = phy_net_listen(options->bind_address, options->port);

    if (listen_fd < 0) {
        trace_log("RECEPTOR", "No se pudo escuchar en %s:%d", options->bind_address, options->port);
        return 1;
    }
    trace_log("RECEPTOR", "Escuchando en %s:%d encoding=%s crc=%s lanes=%d",
              options->bind_address, options->port, phy_encoding_name(options->encoding),
              phy_crc_name(options->crc), options->lanes);

    for (;;) {
        int fd = phy_net_accept(listen_fd);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        handle_connection(fd, options);
        close(fd);
    }

    close(listen_fd);
    return 0;
}

int main(int argc, char **argv)
{
    options_t options;

    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 1;
    }
    if (options.show_help) {
        print_usage(argv[0]);
        return 0;
    }
    trace_enabled = options.trace_enabled;

    if (options.connect_mode) {
        return run_connect(&options);
    }
    return run_serve(&options);
}
