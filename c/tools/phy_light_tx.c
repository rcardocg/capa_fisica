#define _POSIX_C_SOURCE 200809L

#include "phy_tcp_channel.h"
#include "phy_tx.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *host;
    int port;
    const char *message;
    phy_encoding_t encoding;
    phy_crc_algorithm_t crc;
    int seq_start;
    int show_help;
} options_t;

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

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Uso: %s [opciones]\n"
            "\n"
            "Construye tramas PHY y las envia al LED a traves del servidor\n"
            "de la Beagle, que se encarga de compartir los bits.\n"
            "\n"
            "Opciones:\n"
            "  --host DIRECCION    servidor Beagle (defecto 192.168.7.2)\n"
            "  --port PUERTO       puerto del servidor Beagle (defecto 7000)\n"
            "  --message TEXTO     envia un solo mensaje y termina\n"
            "  --encoding utf-8|latin-1  codificacion (defecto utf-8)\n"
            "  --crc NOMBRE        smbus|maxim-dow|sae-j1850|autosar\n"
            "  --seq-start N       secuencia inicial 0..15 (defecto 0)\n"
            "  --help              muestra esta ayuda\n"
            "\n"
            "Sin --message lee mensajes por stdin, uno por linea.\n",
            program);
}

static int parse_options(int argc, char **argv, options_t *options)
{
    static const struct option long_options[] = {
        {"host", required_argument, NULL, 'H'},
        {"port", required_argument, NULL, 'p'},
        {"message", required_argument, NULL, 'm'},
        {"encoding", required_argument, NULL, 'e'},
        {"crc", required_argument, NULL, 'c'},
        {"seq-start", required_argument, NULL, 's'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int option;

    options->host = "192.168.7.2";
    options->port = 7000;
    options->message = NULL;
    options->encoding = PHY_ENCODING_UTF8;
    options->crc = PHY_CRC_SMBUS;
    options->seq_start = 0;
    options->show_help = 0;

    opterr = 0;
    optind = 1;
    while ((option = getopt_long(argc, argv, "H:p:m:e:c:s:h", long_options, NULL)) != -1) {
        switch (option) {
        case 'H':
            options->host = optarg;
            break;
        case 'p':
            if (!parse_int(optarg, &options->port)) {
                return 0;
            }
            break;
        case 'm':
            options->message = optarg;
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
        case 's':
            if (!parse_int(optarg, &options->seq_start)) {
                return 0;
            }
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
    if (options->seq_start < 0 || options->seq_start > 15) {
        return 0;
    }
    return 1;
}

static int run_interactive(phy_channel_t *channel, const phy_tx_options_t *tx_options)
{
    char *line = NULL;
    size_t capacity = 0;

    for (;;) {
        ssize_t length;

        fprintf(stderr, "mensaje> ");
        fflush(stderr);
        length = getline(&line, &capacity, stdin);
        if (length < 0) {
            break;
        }
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (!phy_tx_message(channel, line, tx_options)) {
            free(line);
            return 0;
        }
    }
    free(line);
    return 1;
}

int main(int argc, char **argv)
{
    options_t options;
    phy_tcp_channel_t channel;
    phy_tx_options_t tx_options;
    int result;

    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }
    if (options.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (!phy_tcp_channel_open(&channel, options.host, options.port)) {
        fprintf(stderr, "No se pudo conectar al servidor Beagle en %s:%d\n",
                options.host, options.port);
        return 1;
    }

    tx_options.encoding = options.encoding;
    tx_options.crc = options.crc;
    tx_options.seq_start = options.seq_start;

    printf("Emisor de tramas - canal luz via %s:%d\n", options.host, options.port);
    printf("encoding=%s crc=%s seq=%d\n",
           phy_encoding_name(options.encoding), phy_crc_name(options.crc),
           options.seq_start);
    fflush(stdout);

    if (options.message != NULL) {
        result = phy_tx_message(&channel.base, options.message, &tx_options);
    } else {
        result = run_interactive(&channel.base, &tx_options);
    }

    phy_channel_close(&channel.base);
    return result ? 0 : 1;
}
