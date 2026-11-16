#define _POSIX_C_SOURCE 200809L

#include "phy_analog.h"
#include "phy_gpio.h"
#include "phy_sample.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CLOCK_GPIO 44
#define LED_GPIO 60
#define ADC_AIN 0
#define ON_LEVEL 2100
#define OFF_LEVEL 1700
#define SETTLE_PERCENT 40
#define SAMPLE_COUNT 5
#define SAMPLE_INTERVAL_MS 2
#define MAX_PATTERN 128

typedef struct {
    char pattern[MAX_PATTERN];
    int pattern_length;
    double clock_hz;
    long bit_limit;
    int show_help;
} options_t;

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static double monotonic_seconds(void)
{
    struct timespec timestamp;

    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (double)timestamp.tv_sec + (double)timestamp.tv_nsec / 1000000000.0;
}

static void sleep_ms(int milliseconds)
{
    struct timespec delay;

    if (milliseconds <= 0) {
        return;
    }
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    nanosleep(&delay, NULL);
}

static int parse_double(const char *text, double *out)
{
    char *end = NULL;
    double value;

    if (text == NULL || out == NULL || *text == '\0') {
        return 0;
    }
    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || value <= 0.0) {
        return 0;
    }
    *out = value;
    return 1;
}

static int parse_long(const char *text, long *out)
{
    char *end = NULL;
    long value;

    if (text == NULL || out == NULL || *text == '\0') {
        return 0;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0) {
        return 0;
    }
    *out = value;
    return 1;
}

static int parse_pattern(const char *text, options_t *options)
{
    size_t length;
    size_t index;

    if (text == NULL || *text == '\0') {
        return 0;
    }
    length = strlen(text);
    if (length >= sizeof(options->pattern)) {
        return 0;
    }
    for (index = 0; index < length; index++) {
        if (text[index] != '0' && text[index] != '1') {
            return 0;
        }
        options->pattern[index] = text[index];
    }
    options->pattern[length] = '\0';
    options->pattern_length = (int)length;
    return 1;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Uso: %s [opciones]\n"
            "\n"
            "Genera un patron de luz y lo recibe usando el reloj externo gpio%d.\n"
            "Cada bit dura dos medios ciclos de reloj (dos pulsos).\n"
            "  reloj : gpio%d (salida)\n"
            "  LED   : gpio%d (dato de luz)\n"
            "  ADC   : AIN%d\n"
            "\n"
            "Opciones:\n"
            "  --pattern BITS  secuencia de 0 y 1 (defecto 1110)\n"
            "  --clock-hz HZ   frecuencia de los pulsos de reloj (defecto 2.0)\n"
            "  --bits N        termina despues de N bits (0 = infinito)\n"
            "  --help          muestra esta ayuda\n"
            "\n"
            "Ejemplos:\n"
            "  %s --pattern 1110 --clock-hz 2\n"
            "  %s --pattern 1010 --clock-hz 5 --bits 40\n"
            "\n"
            "Si falla el acceso a /sys, ejecutar con sudo:\n"
            "  sudo %s --pattern 1110\n",
            program, CLOCK_GPIO, CLOCK_GPIO, LED_GPIO, ADC_AIN,
            program, program, program);
}

static int parse_options(int argc, char **argv, options_t *options)
{
    static const struct option long_options[] = {
        {"pattern", required_argument, NULL, 'p'},
        {"clock-hz", required_argument, NULL, 'c'},
        {"bits", required_argument, NULL, 'n'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int option;

    snprintf(options->pattern, sizeof(options->pattern), "1110");
    options->pattern_length = 4;
    options->clock_hz = 2.0;
    options->bit_limit = 0;
    options->show_help = 0;

    opterr = 0;
    optind = 1;
    while ((option = getopt_long(argc, argv, "p:c:n:h", long_options, NULL)) != -1) {
        switch (option) {
        case 'p':
            if (!parse_pattern(optarg, options)) {
                return 0;
            }
            break;
        case 'c':
            if (!parse_double(optarg, &options->clock_hz)) {
                return 0;
            }
            break;
        case 'n':
            if (!parse_long(optarg, &options->bit_limit)) {
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

    if (options->clock_hz < 0.1 || options->clock_hz > 20.0) {
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    options_t options;
    phy_gpio_t clock_pin;
    phy_gpio_t led_pin;
    char adc_path[PATH_MAX];
    double start_time;
    int half_ms;
    int settle_ms;
    int sample_ms;
    int remaining_ms;
    int index = 0;
    int previous_sample = 0;
    int held_bit = 0;
    long bit_count = 0;
    int probe_raw;

    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }
    if (options.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (!phy_analog_find_raw(ADC_AIN, adc_path, sizeof(adc_path))) {
        fprintf(stderr, "No se encontro in_voltage%d_raw para AIN%d.\n", ADC_AIN, ADC_AIN);
        fprintf(stderr, "Revisar: ls /sys/bus/iio/devices\n");
        return 1;
    }
    if (phy_analog_read_raw(adc_path, &probe_raw) != 0) {
        fprintf(stderr, "No se pudo leer %s: %s\n", adc_path, strerror(errno));
        fprintf(stderr, "Probar con sudo o revisar el pin mux del ADC.\n");
        return 1;
    }

    if (phy_gpio_open_output(&clock_pin, CLOCK_GPIO) != 0) {
        fprintf(stderr, "No se pudo abrir clock gpio%d: %s\n", CLOCK_GPIO, strerror(errno));
        fprintf(stderr, "Probar con sudo o verificar /sys/class/gpio.\n");
        return 1;
    }
    if (phy_gpio_open_output(&led_pin, LED_GPIO) != 0) {
        fprintf(stderr, "No se pudo abrir LED gpio%d: %s\n", LED_GPIO, strerror(errno));
        phy_gpio_close(&clock_pin);
        return 1;
    }
    if (phy_gpio_write(&clock_pin, 0) != 0 || phy_gpio_write(&led_pin, 0) != 0) {
        fprintf(stderr, "No se pudo inicializar los GPIO: %s\n", strerror(errno));
        phy_gpio_close(&led_pin);
        phy_gpio_close(&clock_pin);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    half_ms = (int)(1000.0 / options.clock_hz + 0.5);
    if (half_ms < 20) {
        half_ms = 20;
    }
    settle_ms = half_ms * SETTLE_PERCENT / 100;
    sample_ms = (SAMPLE_COUNT - 1) * SAMPLE_INTERVAL_MS;
    remaining_ms = half_ms - settle_ms - sample_ms;
    if (remaining_ms < 0) {
        remaining_ms = 0;
    }

    printf("Enlace de luz - clock gpio%d, LED gpio%d, ADC AIN%d (%s)\n",
           CLOCK_GPIO, LED_GPIO, ADC_AIN, adc_path);
    printf("  Patron     : %s (repetido)\n", options.pattern);
    printf("  Clock      : %.2f Hz, dos pulsos por bit\n", options.clock_hz);
    printf("  Fase       : settle %d ms, muestra %d ms\n", settle_ms, sample_ms);
    printf("  Umbrales   : ON >= %d, OFF <= %d\n\n", ON_LEVEL, OFF_LEVEL);
    fflush(stdout);

    start_time = monotonic_seconds();
    while (!stop_requested) {
        int expected = options.pattern[index] - '0';
        int average[2] = {0, 0};
        int sample[2] = {0, 0};
        int received;
        int half;

        if (phy_gpio_write(&led_pin, expected) != 0) {
            fprintf(stderr, "No se pudo escribir el LED: %s\n", strerror(errno));
            break;
        }

        for (half = 0; half < 2; half++) {
            if (phy_gpio_write(&clock_pin, 1) != 0) {
                fprintf(stderr, "No se pudo subir el clock: %s\n", strerror(errno));
                stop_requested = 1;
                break;
            }
            sleep_ms(settle_ms);
            if (phy_analog_read_average(adc_path, SAMPLE_COUNT,
                                        SAMPLE_INTERVAL_MS, &average[half]) != 0) {
                fprintf(stderr, "Error leyendo %s: %s\n", adc_path, strerror(errno));
                stop_requested = 1;
                break;
            }
            sample[half] = phy_sample_classify(average[half], previous_sample,
                                               ON_LEVEL, OFF_LEVEL);
            previous_sample = sample[half];
            if (phy_gpio_write(&clock_pin, 0) != 0) {
                fprintf(stderr, "No se pudo bajar el clock: %s\n", strerror(errno));
                stop_requested = 1;
                break;
            }
            sleep_ms(remaining_ms);
        }
        if (stop_requested) {
            break;
        }

        received = phy_sample_decode_pair(sample[0], sample[1]);
        held_bit = received;
        printf("[%8.3f s] bit=%3ld esperado=%d s1=%d s2=%d recibido=%d %s\n",
               monotonic_seconds() - start_time, bit_count, expected,
               sample[0], sample[1], held_bit,
               held_bit == expected ? "OK" : "ERROR");
        fflush(stdout);

        bit_count++;
        index = (index + 1) % options.pattern_length;
        if (options.bit_limit > 0 && bit_count >= options.bit_limit) {
            break;
        }
    }

    phy_gpio_close(&led_pin);
    phy_gpio_close(&clock_pin);
    return 0;
}
