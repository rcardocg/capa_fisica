#define _POSIX_C_SOURCE 200809L

#include "phy_gpio_tx_channel.h"
#include "phy_light_rx_channel.h"
#include "phy_node.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char role;
    int tx_clock;
    int rx_clock;
    int data_gpio;
    int ain;
    int clock_hz;
    int on_level;
    int off_level;
    int adc_samples;
    int adc_interval_ms;
    const char *bind_host;
    int port;
    phy_encoding_t encoding;
    phy_crc_algorithm_t crc;
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
    if (errno != 0 || end == text || *end != '\0' ||
        value < -2147483647L - 1L || value > 2147483647L) {
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
            "Nodo C de la capa fisica con API TCP, hilo de recepcion y cola de\n"
            "transmision. Usa el enlace de luz por GPIO/ADC.\n"
            "\n"
            "Opciones:\n"
            "  --role A|B               perfil de pines (defecto A)\n"
            "  --tx-clock GPIO          reloj de transmision (A=44, B=45)\n"
            "  --rx-clock GPIO          reloj de recepcion (A=45, B=44)\n"
            "  --data-gpio GPIO         dato de transmision (defecto 60)\n"
            "  --ain N                  entrada ADC de recepcion (defecto 0)\n"
            "  --clock-hz N             pulsos de reloj por segundo (defecto 20)\n"
            "  --on-level N             umbral de luz encendida (defecto 2100)\n"
            "  --off-level N            umbral de luz apagada (defecto 1700)\n"
            "  --adc-samples N          muestras por pulso (defecto 5)\n"
            "  --adc-interval-ms N      espera entre muestras (defecto 2)\n"
            "  --bind DIRECCION         interfaz de la API (defecto 0.0.0.0)\n"
            "  --port PUERTO            puerto TCP de la API (defecto 7000)\n"
            "  --encoding utf-8|latin-1 codificacion (defecto latin-1)\n"
            "  --crc NOMBRE             smbus|maxim-dow|sae-j1850|autosar\n"
            "  --trace                  muestra los eventos del nodo\n"
            "  --help                   muestra esta ayuda\n"
            "\n"
            "La API acepta PING, HELLO, BITS, SEND, POLL, STATUS y QUIT.\n",
            program);
}

static int parse_options(int argc, char **argv, options_t *options)
{
    enum {
        OPT_ROLE = 1000,
        OPT_TX_CLOCK,
        OPT_RX_CLOCK,
        OPT_DATA_GPIO,
        OPT_AIN,
        OPT_CLOCK_HZ,
        OPT_ON_LEVEL,
        OPT_OFF_LEVEL,
        OPT_ADC_SAMPLES,
        OPT_ADC_INTERVAL,
        OPT_BIND,
        OPT_PORT,
        OPT_ENCODING,
        OPT_CRC,
        OPT_TRACE,
        OPT_HELP
    };
    static const struct option long_options[] = {
        {"role", required_argument, NULL, OPT_ROLE},
        {"tx-clock", required_argument, NULL, OPT_TX_CLOCK},
        {"rx-clock", required_argument, NULL, OPT_RX_CLOCK},
        {"data-gpio", required_argument, NULL, OPT_DATA_GPIO},
        {"ain", required_argument, NULL, OPT_AIN},
        {"clock-hz", required_argument, NULL, OPT_CLOCK_HZ},
        {"on-level", required_argument, NULL, OPT_ON_LEVEL},
        {"off-level", required_argument, NULL, OPT_OFF_LEVEL},
        {"adc-samples", required_argument, NULL, OPT_ADC_SAMPLES},
        {"adc-interval-ms", required_argument, NULL, OPT_ADC_INTERVAL},
        {"bind", required_argument, NULL, OPT_BIND},
        {"port", required_argument, NULL, OPT_PORT},
        {"encoding", required_argument, NULL, OPT_ENCODING},
        {"crc", required_argument, NULL, OPT_CRC},
        {"trace", no_argument, NULL, OPT_TRACE},
        {"help", no_argument, NULL, OPT_HELP},
        {NULL, 0, NULL, 0}
    };
    int option;

    options->role = 'A';
    options->tx_clock = -1;
    options->rx_clock = -1;
    options->data_gpio = 60;
    options->ain = 0;
    options->clock_hz = 20;
    options->on_level = 2100;
    options->off_level = 1700;
    options->adc_samples = 5;
    options->adc_interval_ms = 2;
    options->bind_host = "0.0.0.0";
    options->port = 7000;
    options->encoding = PHY_ENCODING_LATIN1;
    options->crc = PHY_CRC_SMBUS;
    options->trace = 0;
    options->show_help = 0;

    opterr = 0;
    optind = 1;
    while ((option = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (option) {
        case OPT_ROLE:
            if (optarg[0] == 'A' || optarg[0] == 'a') {
                options->role = 'A';
            } else if (optarg[0] == 'B' || optarg[0] == 'b') {
                options->role = 'B';
            } else {
                return 0;
            }
            break;
        case OPT_TX_CLOCK:
            if (!parse_int(optarg, &options->tx_clock)) {
                return 0;
            }
            break;
        case OPT_RX_CLOCK:
            if (!parse_int(optarg, &options->rx_clock)) {
                return 0;
            }
            break;
        case OPT_DATA_GPIO:
            if (!parse_int(optarg, &options->data_gpio)) {
                return 0;
            }
            break;
        case OPT_AIN:
            if (!parse_int(optarg, &options->ain)) {
                return 0;
            }
            break;
        case OPT_CLOCK_HZ:
            if (!parse_int(optarg, &options->clock_hz)) {
                return 0;
            }
            break;
        case OPT_ON_LEVEL:
            if (!parse_int(optarg, &options->on_level)) {
                return 0;
            }
            break;
        case OPT_OFF_LEVEL:
            if (!parse_int(optarg, &options->off_level)) {
                return 0;
            }
            break;
        case OPT_ADC_SAMPLES:
            if (!parse_int(optarg, &options->adc_samples)) {
                return 0;
            }
            break;
        case OPT_ADC_INTERVAL:
            if (!parse_int(optarg, &options->adc_interval_ms)) {
                return 0;
            }
            break;
        case OPT_BIND:
            options->bind_host = optarg;
            break;
        case OPT_PORT:
            if (!parse_int(optarg, &options->port)) {
                return 0;
            }
            break;
        case OPT_ENCODING:
            if (!phy_encoding_parse(optarg, &options->encoding)) {
                return 0;
            }
            break;
        case OPT_CRC:
            if (!phy_crc_parse(optarg, &options->crc)) {
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

    if (options->tx_clock < 0) {
        options->tx_clock = options->role == 'A' ? 44 : 45;
    }
    if (options->rx_clock < 0) {
        options->rx_clock = options->role == 'A' ? 45 : 44;
    }
    if (options->tx_clock < 0 || options->rx_clock < 0 ||
        options->data_gpio < 0 || options->ain < 0) {
        return 0;
    }
    if (options->clock_hz < 1 || options->clock_hz > 1000) {
        return 0;
    }
    if (options->adc_samples < 1 || options->adc_samples > 512) {
        return 0;
    }
    if (options->adc_interval_ms < 0 || options->adc_interval_ms > 1000) {
        return 0;
    }
    if (options->on_level <= options->off_level) {
        return 0;
    }
    if (options->port < 0 || options->port > 65535) {
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    options_t options;
    phy_gpio_tx_channel_t tx_channel;
    phy_light_rx_channel_t rx_channel;
    phy_light_rx_channel_config_t rx_config;
    phy_node_config_t node_config;
    phy_node_t *node = NULL;
    int tx_ready = 0;
    int rx_ready = 0;

    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }
    if (options.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (!phy_gpio_tx_channel_open(&tx_channel, options.tx_clock,
                                  options.data_gpio, options.clock_hz)) {
        fprintf(stderr, "No se pudo abrir clock gpio%d y LED gpio%d: %s\n",
                options.tx_clock, options.data_gpio, strerror(errno));
        fprintf(stderr, "Probar con sudo o verificar /sys/class/gpio.\n");
        return 1;
    }
    tx_ready = 1;

    rx_config.clock_gpio = options.rx_clock;
    rx_config.ain = options.ain;
    rx_config.clock_hz = options.clock_hz;
    rx_config.on_level = options.on_level;
    rx_config.off_level = options.off_level;
    rx_config.adc_samples = options.adc_samples;
    rx_config.adc_interval_ms = options.adc_interval_ms;
    if (!phy_light_rx_channel_open(&rx_channel, &rx_config)) {
        fprintf(stderr, "No se pudo abrir el receptor de luz (clock gpio%d, AIN%d): %s\n",
                options.rx_clock, options.ain, strerror(errno));
        fprintf(stderr, "Probar con sudo o revisar /sys/bus/iio/devices.\n");
        phy_channel_close(phy_gpio_tx_channel_base(&tx_channel));
        return 1;
    }
    rx_ready = 1;

    memset(&node_config, 0, sizeof(node_config));
    node_config.name = options.role == 'A' ? "A" : "B";
    node_config.tx = phy_gpio_tx_channel_base(&tx_channel);
    node_config.rx = phy_light_rx_channel_base(&rx_channel);
    node_config.crc = options.crc;
    node_config.encoding = options.encoding;
    node_config.bind_host = options.bind_host;
    node_config.port = options.port;
    node_config.rx_timeout_ms = 200;
    node_config.trace = options.trace;

    if (!phy_node_start(&node, &node_config)) {
        fprintf(stderr, "No se pudo iniciar el nodo en %s:%d: %s\n",
                options.bind_host, options.port, strerror(errno));
        if (rx_ready) {
            phy_rx_channel_close(phy_light_rx_channel_base(&rx_channel));
        }
        if (tx_ready) {
            phy_channel_close(phy_gpio_tx_channel_base(&tx_channel));
        }
        return 1;
    }

    printf("Nodo %s escuchando en %s:%d\n",
           node_config.name, options.bind_host, phy_node_port(node));
    printf("  TX clock gpio%d, LED gpio%d\n", options.tx_clock, options.data_gpio);
    printf("  RX clock gpio%d, AIN%d, umbrales ON>=%d OFF<=%d\n",
           options.rx_clock, options.ain, options.on_level, options.off_level);
    printf("  clock_hz=%d, encoding=%s, crc=%s\n",
           options.clock_hz, phy_encoding_name(options.encoding),
           phy_crc_name(options.crc));
    fflush(stdout);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    while (!stop_requested) {
        struct timespec delay;

        delay.tv_sec = 1;
        delay.tv_nsec = 0;
        nanosleep(&delay, NULL);
    }

    phy_node_stop(node);
    if (rx_ready) {
        phy_rx_channel_close(phy_light_rx_channel_base(&rx_channel));
    }
    if (tx_ready) {
        phy_channel_close(phy_gpio_tx_channel_base(&tx_channel));
    }
    printf("Nodo %s detenido\n", node_config.name);
    return 0;
}
