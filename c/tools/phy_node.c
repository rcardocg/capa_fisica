#define _POSIX_C_SOURCE 200809L

#include "phy_gpio_tx_channel.h"
#include "phy_light_rx_channel.h"
#include "phy_node.h"
#include "phy_piezo_rx_channel.h"
#include "phy_piezo_tx_channel.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    MEDIUM_LIGHT = 0,
    MEDIUM_PIEZO = 1
};

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
    int tx_medium;
    int rx_medium;
    int piezo_gpio;
    int piezo_ain;
    int piezo_carrier_hz;
    int piezo_on_level;
    int piezo_off_level;
    int piezo_baseline_shift;
    const char *bind_host;
    int port;
    phy_encoding_t encoding;
    phy_crc_algorithm_t crc;
    int trace;
    int ack_enabled;
    int ack_timeout_ms;
    int ack_retries;
    int handshake_enabled;
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

static int parse_medium(const char *text, int *out)
{
    if (text == NULL || out == NULL) {
        return 0;
    }
    if (strcmp(text, "light") == 0 || strcmp(text, "luz") == 0) {
        *out = MEDIUM_LIGHT;
        return 1;
    }
    if (strcmp(text, "piezo") == 0 || strcmp(text, "sonido") == 0) {
        *out = MEDIUM_PIEZO;
        return 1;
    }
    return 0;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Uso: %s [opciones]\n"
            "\n"
            "Nodo C de la capa fisica con API TCP, hilo de recepcion y cola de\n"
            "transmision. Usa el enlace optico o el receptor piezoelectrico.\n"
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
            "  --rx-medium light|piezo  medio de recepcion (defecto light)\n"
            "  --tx-medium light|piezo  medio de transmision (defecto light)\n"
            "  --piezo-ain N            ADC del piezo, distinto de AIN0 (defecto 1)\n"
            "  --piezo-gpio GPIO        dato TX del piezo (defecto 50 / P9_14)\n"
            "  --piezo-carrier-hz N     portadora del piezo (defecto 1000)\n"
            "  --piezo-on-level N       umbral de envolvente ON (defecto 120)\n"
            "  --piezo-off-level N      umbral de envolvente OFF (defecto 60)\n"
            "  --piezo-baseline-shift N divisor de seguimiento de linea base (defecto 6)\n"
            "  --bind DIRECCION         interfaz de la API (defecto 0.0.0.0)\n"
            "  --port PUERTO            puerto TCP de la API (defecto 7000)\n"
            "  --encoding utf-8|latin-1 codificacion (defecto latin-1)\n"
            "  --crc NOMBRE             smbus|maxim-dow|sae-j1850|autosar\n"
            "  --ack-timeout-ms N       espera de ACK/NACK (0 = automatico)\n"
            "  --ack-retries N          reintentos por frame (defecto 3)\n"
            "  --no-ack                 desactiva ACK/NACK\n"
            "  --no-handshake           salta el handshake SYN/SYN-ACK/ACK\n"
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
        OPT_RX_MEDIUM,
        OPT_TX_MEDIUM,
        OPT_PIEZO_AIN,
        OPT_PIEZO_GPIO,
        OPT_PIEZO_CARRIER,
        OPT_PIEZO_ON_LEVEL,
        OPT_PIEZO_OFF_LEVEL,
        OPT_PIEZO_BASELINE_SHIFT,
        OPT_BIND,
        OPT_PORT,
        OPT_ENCODING,
        OPT_CRC,
        OPT_ACK_TIMEOUT,
        OPT_ACK_RETRIES,
        OPT_NO_ACK,
        OPT_NO_HANDSHAKE,
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
        {"rx-medium", required_argument, NULL, OPT_RX_MEDIUM},
        {"tx-medium", required_argument, NULL, OPT_TX_MEDIUM},
        {"piezo-ain", required_argument, NULL, OPT_PIEZO_AIN},
        {"piezo-gpio", required_argument, NULL, OPT_PIEZO_GPIO},
        {"piezo-carrier-hz", required_argument, NULL, OPT_PIEZO_CARRIER},
        {"piezo-on-level", required_argument, NULL, OPT_PIEZO_ON_LEVEL},
        {"piezo-off-level", required_argument, NULL, OPT_PIEZO_OFF_LEVEL},
        {"piezo-baseline-shift", required_argument, NULL, OPT_PIEZO_BASELINE_SHIFT},
        {"bind", required_argument, NULL, OPT_BIND},
        {"port", required_argument, NULL, OPT_PORT},
        {"encoding", required_argument, NULL, OPT_ENCODING},
        {"crc", required_argument, NULL, OPT_CRC},
        {"ack-timeout-ms", required_argument, NULL, OPT_ACK_TIMEOUT},
        {"ack-retries", required_argument, NULL, OPT_ACK_RETRIES},
        {"no-ack", no_argument, NULL, OPT_NO_ACK},
        {"no-handshake", no_argument, NULL, OPT_NO_HANDSHAKE},
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
    options->tx_medium = MEDIUM_LIGHT;
    options->rx_medium = MEDIUM_LIGHT;
    options->piezo_gpio = 50;
    options->piezo_ain = 1;
    options->piezo_carrier_hz = 1000;
    options->piezo_on_level = 120;
    options->piezo_off_level = 60;
    options->piezo_baseline_shift = 6;
    options->bind_host = "0.0.0.0";
    options->port = 7000;
    options->encoding = PHY_ENCODING_LATIN1;
    options->crc = PHY_CRC_SMBUS;
    options->trace = 0;
    options->ack_enabled = 1;
    options->ack_timeout_ms = 0;
    options->ack_retries = 3;
    options->handshake_enabled = 1;
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
        case OPT_RX_MEDIUM:
            if (!parse_medium(optarg, &options->rx_medium)) {
                return 0;
            }
            break;
        case OPT_TX_MEDIUM:
            if (!parse_medium(optarg, &options->tx_medium)) {
                return 0;
            }
            break;
        case OPT_PIEZO_AIN:
            if (!parse_int(optarg, &options->piezo_ain)) {
                return 0;
            }
            break;
        case OPT_PIEZO_GPIO:
            if (!parse_int(optarg, &options->piezo_gpio)) {
                return 0;
            }
            break;
        case OPT_PIEZO_CARRIER:
            if (!parse_int(optarg, &options->piezo_carrier_hz)) {
                return 0;
            }
            break;
        case OPT_PIEZO_ON_LEVEL:
            if (!parse_int(optarg, &options->piezo_on_level)) {
                return 0;
            }
            break;
        case OPT_PIEZO_OFF_LEVEL:
            if (!parse_int(optarg, &options->piezo_off_level)) {
                return 0;
            }
            break;
        case OPT_PIEZO_BASELINE_SHIFT:
            if (!parse_int(optarg, &options->piezo_baseline_shift)) {
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
        case OPT_ACK_TIMEOUT:
            if (!parse_int(optarg, &options->ack_timeout_ms)) {
                return 0;
            }
            break;
        case OPT_ACK_RETRIES:
            if (!parse_int(optarg, &options->ack_retries)) {
                return 0;
            }
            break;
        case OPT_NO_ACK:
            options->ack_enabled = 0;
            break;
        case OPT_NO_HANDSHAKE:
            options->handshake_enabled = 0;
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
    if (options->piezo_ain < 0 || options->piezo_ain > 6) {
        return 0;
    }
    if (options->rx_medium == MEDIUM_PIEZO && options->piezo_ain == 0) {
        return 0;
    }
    if (options->piezo_on_level <= options->piezo_off_level) {
        return 0;
    }
    if (options->piezo_off_level < 0 || options->piezo_on_level > 4095) {
        return 0;
    }
    if (options->piezo_gpio < 0 || options->piezo_carrier_hz < 1 ||
        options->piezo_carrier_hz > 20000) {
        return 0;
    }
    if (options->piezo_baseline_shift < 1 || options->piezo_baseline_shift > 12) {
        return 0;
    }
    if (options->tx_medium == MEDIUM_PIEZO &&
        (options->piezo_gpio == options->tx_clock ||
         options->piezo_gpio == options->rx_clock ||
         options->piezo_gpio == options->data_gpio)) {
        return 0;
    }
    if (options->port < 0 || options->port > 65535) {
        return 0;
    }
    if (options->ack_retries < 1 || options->ack_retries > 10) {
        return 0;
    }
    if (options->ack_timeout_ms < 0) {
        return 0;
    }
    if (options->ack_timeout_ms == 0) {
        int frame_ms = (int)((2000L * PHY_FRAME_BITS +
                              options->clock_hz - 1) / options->clock_hz);

        options->ack_timeout_ms = 2 * frame_ms + 1000;
    }
    return 1;
}

int main(int argc, char **argv)
{
    options_t options;
    phy_gpio_tx_channel_t light_tx;
    phy_piezo_tx_channel_t piezo_tx;
    phy_light_rx_channel_t light_rx;
    phy_piezo_rx_channel_t piezo_rx;
    phy_light_rx_channel_config_t light_config;
    phy_piezo_rx_channel_config_t piezo_config;
    phy_channel_t *tx_base = NULL;
    phy_rx_channel_t *rx_base = NULL;
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

    if (options.tx_medium == MEDIUM_PIEZO) {
        if (!phy_piezo_tx_channel_open(&piezo_tx, options.tx_clock,
                                       options.piezo_gpio, options.clock_hz,
                                       options.piezo_carrier_hz)) {
            fprintf(stderr,
                    "No se pudo abrir el transmisor piezo (clock gpio%d, dato gpio%d): %s\n",
                    options.tx_clock, options.piezo_gpio, strerror(errno));
            return 1;
        }
        tx_base = phy_piezo_tx_channel_base(&piezo_tx);
    } else {
        if (!phy_gpio_tx_channel_open(&light_tx, options.tx_clock,
                                      options.data_gpio, options.clock_hz)) {
            fprintf(stderr, "No se pudo abrir clock gpio%d y LED gpio%d: %s\n",
                    options.tx_clock, options.data_gpio, strerror(errno));
            fprintf(stderr, "Probar con sudo o verificar /sys/class/gpio.\n");
            return 1;
        }
        tx_base = phy_gpio_tx_channel_base(&light_tx);
    }
    tx_ready = 1;

    if (options.rx_medium == MEDIUM_PIEZO) {
        piezo_config.clock_gpio = options.rx_clock;
        piezo_config.ain = options.piezo_ain;
        piezo_config.clock_hz = options.clock_hz;
        piezo_config.on_level = options.piezo_on_level;
        piezo_config.off_level = options.piezo_off_level;
        piezo_config.adc_samples = options.adc_samples;
        piezo_config.adc_interval_ms = options.adc_interval_ms;
        piezo_config.baseline_shift = options.piezo_baseline_shift;
        if (!phy_piezo_rx_channel_open(&piezo_rx, &piezo_config)) {
            fprintf(stderr,
                    "No se pudo abrir el receptor piezo (clock gpio%d, AIN%d): %s\n",
                    options.rx_clock, options.piezo_ain, strerror(errno));
            fprintf(stderr, "Probar con sudo o revisar /sys/bus/iio/devices.\n");
            phy_channel_close(tx_base);
            return 1;
        }
        rx_base = phy_piezo_rx_channel_base(&piezo_rx);
    } else {
        light_config.clock_gpio = options.rx_clock;
        light_config.ain = options.ain;
        light_config.clock_hz = options.clock_hz;
        light_config.on_level = options.on_level;
        light_config.off_level = options.off_level;
        light_config.adc_samples = options.adc_samples;
        light_config.adc_interval_ms = options.adc_interval_ms;
        if (!phy_light_rx_channel_open(&light_rx, &light_config)) {
            fprintf(stderr, "No se pudo abrir el receptor de luz (clock gpio%d, AIN%d): %s\n",
                    options.rx_clock, options.ain, strerror(errno));
            fprintf(stderr, "Probar con sudo o revisar /sys/bus/iio/devices.\n");
            phy_channel_close(tx_base);
            return 1;
        }
        rx_base = phy_light_rx_channel_base(&light_rx);
    }
    rx_ready = 1;

    memset(&node_config, 0, sizeof(node_config));
    node_config.name = options.role == 'A' ? "A" : "B";
    node_config.tx = tx_base;
    node_config.rx = rx_base;
    node_config.crc = options.crc;
    node_config.encoding = options.encoding;
    node_config.bind_host = options.bind_host;
    node_config.port = options.port;
    node_config.rx_timeout_ms = 200;
    node_config.trace = options.trace;
    node_config.ack_enabled = options.ack_enabled;
    node_config.ack_timeout_ms = options.ack_timeout_ms;
    node_config.ack_retries = options.ack_retries;
    node_config.handshake_enabled = options.handshake_enabled;
    node_config.handshake_initiator = options.role == 'A';

    if (!phy_node_start(&node, &node_config)) {
        fprintf(stderr, "No se pudo iniciar el nodo en %s:%d: %s\n",
                options.bind_host, options.port, strerror(errno));
        if (rx_ready) {
            phy_rx_channel_close(rx_base);
        }
        if (tx_ready) {
            phy_channel_close(tx_base);
        }
        return 1;
    }

    printf("Nodo %s escuchando en %s:%d\n",
           node_config.name, options.bind_host, phy_node_port(node));
    if (options.tx_medium == MEDIUM_PIEZO) {
        printf("  TX clock gpio%d, piezo gpio%d, portadora %d Hz\n",
               options.tx_clock, options.piezo_gpio, options.piezo_carrier_hz);
    } else {
        printf("  TX clock gpio%d, LED gpio%d\n",
               options.tx_clock, options.data_gpio);
    }
    if (options.rx_medium == MEDIUM_PIEZO) {
        printf("  RX clock gpio%d, AIN%d piezo, envolvente ON>=%d OFF<=%d\n",
               options.rx_clock, options.piezo_ain, options.piezo_on_level,
               options.piezo_off_level);
    } else {
        printf("  RX clock gpio%d, AIN%d, umbrales ON>=%d OFF<=%d\n",
               options.rx_clock, options.ain, options.on_level, options.off_level);
    }
    printf("  clock_hz=%d, encoding=%s, crc=%s\n",
           options.clock_hz, phy_encoding_name(options.encoding),
           phy_crc_name(options.crc));
    printf("  ACK=%s timeout=%d ms retries=%d\n",
           options.ack_enabled ? "on" : "off", options.ack_timeout_ms,
           options.ack_retries);
    printf("  handshake=%s rol=%s\n",
           options.handshake_enabled
               ? (options.role == 'A' ? "on (iniciador)" : "on (respondedor)")
               : "off",
           options.role == 'A' ? "A" : "B");
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
        phy_rx_channel_close(rx_base);
    }
    if (tx_ready) {
        phy_channel_close(tx_base);
    }
    printf("Nodo %s detenido\n", node_config.name);
    return 0;
}
