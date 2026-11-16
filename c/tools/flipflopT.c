#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ADC_AIN 0
#define ADC_PIN "P9_39 (AIN0)"
#define LED_GPIO 60
#define DEFAULT_BLINK_HZ 1.0

#define ON_LEVEL 2100
#define OFF_LEVEL 1700
#define SETTLE_PERCENT 40

typedef struct {
    int led_enabled;
    double blink_hz;
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
    struct timespec remaining;

    if (milliseconds <= 0) {
        return;
    }
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    while (nanosleep(&delay, &remaining) != 0) {
        if (errno != EINTR || stop_requested) {
            return;
        }
        delay = remaining;
    }
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

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Uso: %s [opciones]\n"
            "\n"
            "Demodula el bit de luz por medio ciclo y lo guarda en un flip-flop.\n"
            "Descarta el inicio de cada fase (settle) para evitar luz residual.\n"
            "\n"
            "  ADC : AIN0 / %s\n"
            "  LED : gpio%d\n"
            "\n"
            "Opciones:\n"
            "  --led           enciende y apaga el LED para hacer la prueba\n"
            "  --blink HZ      frecuencia de parpadeo (defecto %.1f Hz)\n"
            "  --help          muestra esta ayuda\n"
            "\n"
            "Por cada medio ciclo se clasifica la luz como s=1 (claro) o s=0\n"
            "(oscuro). El par (s anterior, s actual) se decodifica asi:\n"
            "  00 -> bit 0\n"
            "  01 -> bit 1\n"
            "  10 -> bit 0\n"
            "  11 -> bit 1\n"
            "\n"
            "Si falla el acceso a /sys, ejecutar con sudo:\n"
            "  sudo %s --led\n",
            program, ADC_PIN, LED_GPIO, DEFAULT_BLINK_HZ, program);
}

static int parse_options(int argc, char **argv, options_t *options)
{
    static const struct option long_options[] = {
        {"led", no_argument, NULL, 'l'},
        {"blink", required_argument, NULL, 'b'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int option;

    options->led_enabled = 0;
    options->blink_hz = DEFAULT_BLINK_HZ;
    options->show_help = 0;

    opterr = 0;
    optind = 1;
    while ((option = getopt_long(argc, argv, "lb:h", long_options, NULL)) != -1) {
        switch (option) {
        case 'l':
            options->led_enabled = 1;
            break;
        case 'b':
            if (!parse_double(optarg, &options->blink_hz)) {
                return 0;
            }
            options->led_enabled = 1;
            break;
        case 'h':
            options->show_help = 1;
            return 1;
        default:
            return 0;
        }
    }

    if (options->blink_hz < 0.1 || options->blink_hz > 20.0) {
        return 0;
    }
    return 1;
}

static int write_text(const char *path, const char *text)
{
    int descriptor;
    ssize_t expected;
    ssize_t written;
    int saved_errno;

    descriptor = open(path, O_WRONLY);
    if (descriptor < 0) {
        return -1;
    }
    expected = (ssize_t)strlen(text);
    written = write(descriptor, text, (size_t)expected);
    saved_errno = errno;
    close(descriptor);
    errno = saved_errno;
    if (written != expected) {
        if (written >= 0) {
            errno = EIO;
        }
        return -1;
    }
    return 0;
}

static int gpio_setup(int gpio, int *exported_by_us)
{
    char path[PATH_MAX];
    char number[32];

    *exported_by_us = 0;
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", gpio);
    if (access(path, F_OK) != 0) {
        snprintf(number, sizeof(number), "%d", gpio);
        if (write_text("/sys/class/gpio/export", number) != 0) {
            fprintf(stderr, "No se pudo exportar GPIO%d: %s\n", gpio, strerror(errno));
            fprintf(stderr, "Probar con sudo o verificar /sys/class/gpio.\n");
            return -1;
        }
        *exported_by_us = 1;
        sleep_ms(100);
    }

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    if (write_text(path, "out") != 0) {
        fprintf(stderr, "No se pudo configurar GPIO%d como salida: %s\n", gpio, strerror(errno));
        if (*exported_by_us) {
            snprintf(number, sizeof(number), "%d", gpio);
            (void)write_text("/sys/class/gpio/unexport", number);
            *exported_by_us = 0;
        }
        return -1;
    }
    return 0;
}

static int gpio_write_value(int gpio, int value)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    return write_text(path, value ? "1" : "0");
}

static void gpio_cleanup(int gpio, int exported_by_us)
{
    char number[32];
    char path[PATH_MAX];

    (void)gpio_write_value(gpio, 0);
    if (!exported_by_us) {
        return;
    }
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", gpio);
    if (access(path, F_OK) == 0) {
        snprintf(number, sizeof(number), "%d", gpio);
        (void)write_text("/sys/class/gpio/unexport", number);
    }
}

static int find_adc_raw(int ain, char *path, size_t path_size)
{
    int device;
    char candidate[PATH_MAX];

    for (device = 0; device < 10; device++) {
        snprintf(candidate, sizeof(candidate),
                 "/sys/bus/iio/devices/iio:device%d/in_voltage%d_raw", device, ain);
        if (access(candidate, F_OK) == 0) {
            snprintf(path, path_size, "%s", candidate);
            return 1;
        }
    }

    snprintf(candidate, sizeof(candidate),
             "/sys/devices/platform/ocp/44e0d000.tscadc/44e0d000.tscadc:adc/in_voltage%d_raw",
             ain);
    if (access(candidate, F_OK) == 0) {
        snprintf(path, path_size, "%s", candidate);
        return 1;
    }
    return 0;
}

static int read_adc_raw(const char *path, int *raw)
{
    char buffer[64];
    char *end = NULL;
    long value;
    int descriptor;
    ssize_t length;

    descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        return -1;
    }
    length = read(descriptor, buffer, sizeof(buffer) - 1u);
    close(descriptor);
    if (length <= 0) {
        return -1;
    }
    buffer[length] = '\0';
    errno = 0;
    value = strtol(buffer, &end, 10);
    if (errno != 0 || end == buffer) {
        return -1;
    }
    *raw = (int)value;
    return 0;
}

static int classify(int average, int previous)
{
    if (average >= ON_LEVEL) {
        return 1;
    }
    if (average <= OFF_LEVEL) {
        return 0;
    }
    return previous;
}

static int decode_pair(int previous, int current)
{
    if (previous == 0 && current == 0) {
        return 0;
    }
    if (previous == 0 && current == 1) {
        return 1;
    }
    if (previous == 1 && current == 0) {
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    options_t options;
    char adc_path[PATH_MAX];
    int exported_by_us = 0;
    double start_time;
    int period_ms;
    int half_ms;
    int sample_ms;
    int settle_ms;
    int last_blink = -1;
    double phase_start;
    long phase_sum = 0;
    int phase_count = 0;
    int previous_sample = 0;
    int held_bit = 0;
    int probe_raw;

    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }
    if (options.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (!find_adc_raw(ADC_AIN, adc_path, sizeof(adc_path))) {
        fprintf(stderr, "No se encontro in_voltage%d_raw para AIN%d.\n", ADC_AIN, ADC_AIN);
        fprintf(stderr, "Revisar:\n");
        fprintf(stderr, "  ls /sys/bus/iio/devices\n");
        fprintf(stderr, "  find /sys -name 'in_voltage*_raw' 2>/dev/null\n");
        return 1;
    }
    if (read_adc_raw(adc_path, &probe_raw) != 0) {
        fprintf(stderr, "No se pudo leer %s: %s\n", adc_path, strerror(errno));
        fprintf(stderr, "Probar con sudo o revisar el pin mux del ADC.\n");
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (options.led_enabled) {
        if (gpio_setup(LED_GPIO, &exported_by_us) != 0) {
            return 1;
        }
        if (gpio_write_value(LED_GPIO, 0) != 0) {
            fprintf(stderr, "No se pudo apagar GPIO%d: %s\n", LED_GPIO, strerror(errno));
            gpio_cleanup(LED_GPIO, exported_by_us);
            return 1;
        }
    }

    period_ms = (int)(1000.0 / options.blink_hz + 0.5);
    if (period_ms < 20) {
        period_ms = 20;
    }
    half_ms = period_ms / 2;
    settle_ms = half_ms * SETTLE_PERCENT / 100;
    sample_ms = half_ms / 8;
    if (sample_ms < 2) {
        sample_ms = 2;
    }
    if (sample_ms > 50) {
        sample_ms = 50;
    }

    printf("Flip-flop de luz - ADC AIN%d (%s)\n", ADC_AIN, ADC_PIN);
    printf("  ADC        : %s\n", adc_path);
    printf("  LED        : gpio%d, %s\n", LED_GPIO,
           options.led_enabled ? "parpadeando" : "sin --led, las fases seran oscuras");
    printf("  Parpadeo   : %.2f Hz (settle %d ms)\n\n", options.blink_hz, settle_ms);
    fflush(stdout);

    start_time = monotonic_seconds();
    phase_start = start_time;
    while (!stop_requested) {
        double now = monotonic_seconds();
        long elapsed_ms = (long)((now - start_time) * 1000.0);
        int blink = (elapsed_ms % (long)period_ms) < (long)half_ms;
        int raw;

        if (blink != last_blink) {
            if (last_blink >= 0 && phase_count > 0) {
                int average = (int)(phase_sum / phase_count);
                int sample = classify(average, previous_sample);
                int pair_previous = previous_sample;
                int bit = decode_pair(pair_previous, sample);

                previous_sample = sample;
                held_bit = bit;
                printf("[%8.3f s] fase=%s avg=%4d s=%d par=%d%d bit=%d\n",
                       now - start_time, last_blink ? "ON " : "OFF",
                       average, sample, pair_previous, sample, held_bit);
                fflush(stdout);
            }

            phase_sum = 0;
            phase_count = 0;
            phase_start = now;
            last_blink = blink;

            if (options.led_enabled) {
                if (gpio_write_value(LED_GPIO, blink) != 0) {
                    fprintf(stderr, "No se pudo escribir GPIO%d: %s\n",
                            LED_GPIO, strerror(errno));
                    break;
                }
            }
        }

        if (read_adc_raw(adc_path, &raw) != 0) {
            fprintf(stderr, "Error leyendo %s: %s\n", adc_path, strerror(errno));
            break;
        }
        if ((now - phase_start) * 1000.0 >= (double)settle_ms) {
            phase_sum += raw;
            phase_count++;
        }

        sleep_ms(sample_ms);
    }

    if (options.led_enabled) {
        gpio_cleanup(LED_GPIO, exported_by_us);
    }
    return 0;
}
