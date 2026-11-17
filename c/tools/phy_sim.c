#define _POSIX_C_SOURCE 200809L

#include "phy_fake_link.h"
#include "phy_node.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int a_port;
    int b_port;
    int clock_hz;
    int trace;
    int show_help;
} options_t;

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
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
    if (errno != 0 || end == text || *end != '\0' || value < 0 || value > 65535) {
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
            "Levanta dos nodos A y B unidos por el enlace falso en memoria.\n"
            "\n"
            "Opciones:\n"
            "  --a-port N       puerto API de A (defecto 17001)\n"
            "  --b-port N       puerto API de B (defecto 17002)\n"
            "  --clock-hz N     pulsos por segundo del enlace falso (defecto 1000)\n"
            "  --trace          muestra los eventos de A y B\n"
            "  --help           muestra esta ayuda\n",
            program);
}

static int parse_options(int argc, char **argv, options_t *options)
{
    enum {
        OPT_A_PORT = 1000,
        OPT_B_PORT,
        OPT_CLOCK_HZ,
        OPT_TRACE,
        OPT_HELP
    };
    static const struct option long_options[] = {
        {"a-port", required_argument, NULL, OPT_A_PORT},
        {"b-port", required_argument, NULL, OPT_B_PORT},
        {"clock-hz", required_argument, NULL, OPT_CLOCK_HZ},
        {"trace", no_argument, NULL, OPT_TRACE},
        {"help", no_argument, NULL, OPT_HELP},
        {NULL, 0, NULL, 0}
    };
    int option;

    options->a_port = 17001;
    options->b_port = 17002;
    options->clock_hz = 1000;
    options->trace = 0;
    options->show_help = 0;

    opterr = 0;
    optind = 1;
    while ((option = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (option) {
        case OPT_A_PORT:
            if (!parse_int(optarg, &options->a_port)) {
                return 0;
            }
            break;
        case OPT_B_PORT:
            if (!parse_int(optarg, &options->b_port)) {
                return 0;
            }
            break;
        case OPT_CLOCK_HZ:
            if (!parse_int(optarg, &options->clock_hz)) {
                return 0;
            }
            break;
        case OPT_TRACE:
            options->trace = 1;
            break;
        case OPT_HELP:
            options->show_help = 1;
            return 1;
        default:
            return 0;
        }
    }

    if (options->a_port <= 0 || options->b_port <= 0 ||
        options->a_port == options->b_port) {
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    options_t options;
    phy_fake_link_t link;
    phy_node_t *node_a = NULL;
    phy_node_t *node_b = NULL;
    phy_node_config_t config;
    int link_ready = 0;
    int a_ready = 0;
    int b_ready = 0;

    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }
    if (options.show_help) {
        print_usage(argv[0]);
        return 0;
    }
    if (!phy_fake_link_init(&link, 0)) {
        fprintf(stderr, "No se pudo crear el enlace falso\n");
        return 1;
    }
    link_ready = 1;
    if (options.clock_hz > 0 && !phy_fake_link_set_clock_hz(&link, options.clock_hz)) {
        fprintf(stderr, "clock_hz invalido\n");
        phy_fake_link_close(&link);
        return 1;
    }

    memset(&config, 0, sizeof(config));
    config.name = "A";
    config.tx = phy_fake_link_a_tx(&link);
    config.rx = phy_fake_link_a_rx(&link);
    config.crc = PHY_CRC_SMBUS;
    config.encoding = PHY_ENCODING_LATIN1;
    config.bind_host = "127.0.0.1";
    config.port = options.a_port;
    config.rx_timeout_ms = 100;
    config.trace = options.trace;
    config.ack_enabled = 1;
    config.ack_timeout_ms = 1000;
    config.ack_retries = 3;
    config.handshake_enabled = 1;
    config.handshake_initiator = 1;
    if (!phy_node_start(&node_a, &config)) {
        fprintf(stderr, "No se pudo iniciar el nodo A en 127.0.0.1:%d\n", options.a_port);
        phy_fake_link_close(&link);
        return 1;
    }
    a_ready = 1;

    config.name = "B";
    config.tx = phy_fake_link_b_tx(&link);
    config.rx = phy_fake_link_b_rx(&link);
    config.port = options.b_port;
    config.handshake_initiator = 0;
    if (!phy_node_start(&node_b, &config)) {
        fprintf(stderr, "No se pudo iniciar el nodo B en 127.0.0.1:%d\n", options.b_port);
        phy_node_stop(node_a);
        phy_fake_link_close(&link);
        return 1;
    }
    b_ready = 1;

    printf("READY A=%d B=%d clock_hz=%d\n",
           phy_node_port(node_a), phy_node_port(node_b), options.clock_hz);
    fflush(stdout);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    while (!stop_requested) {
        struct timespec delay;

        delay.tv_sec = 0;
        delay.tv_nsec = 200000000L;
        nanosleep(&delay, NULL);
    }

    if (b_ready) {
        phy_node_stop(node_b);
    }
    if (a_ready) {
        phy_node_stop(node_a);
    }
    if (link_ready) {
        phy_fake_link_close(&link);
    }
    return 0;
}
