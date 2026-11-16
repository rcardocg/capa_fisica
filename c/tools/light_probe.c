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

#define PRESENT_HIGH 200
#define PRESENT_LOW 80
#define HISTORY_SIZE 3

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
            "Detecta si el LED esta presente midiendo si la luz del ADC (AIN0 /\n"
            "%s) responde cuando el LED (gpio%d) se enciende y apaga.\n"
            "La deteccion es diferencial: compara la luz con el LED apagado y\n"
            "encendido, asi la luz ambiente constante no cuenta.\n"
            "\n"
            "Opciones:\n"
            "  --led           enciende y apaga el LED para hacer la prueba\n"
            "  --blink HZ      frecuencia de parpadeo (defecto %.1f Hz)\n"
            "  --help          muestra esta ayuda\n"
            "\n"
            "Salida (una por cada medio ciclo de reloj):\n"
            "  1  LED presente\n"
            "  0  LED ausente\n"
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

static int abs_int(int value)
{
    return value < 0 ? -value : value;
}

static int evaluate(const int contrast[HISTORY_SIZE], int *present)
{
    int i;
    int positive = 1;
    int negative = 1;
    int min_abs = INT_MAX;
    int max_abs = 0;

    for (i = 0; i < HISTORY_SIZE; i++) {
        int magnitude = abs_int(contrast[i]);

        if (contrast[i] <= 0) {
            positive = 0;
        }
        if (contrast[i] >= 0) {
            negative = 0;
        }
        if (magnitude < min_abs) {
            min_abs = magnitude;
        }
        if (magnitude > max_abs) {
            max_abs = magnitude;
        }
    }

    if ((positive || negative) && min_abs >= PRESENT_HIGH) {
        *present = 1;
    } else if (!(positive || negative) || max_abs <= PRESENT_LOW) {
        *present = 0;
    } else {
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
    int last_blink = -1;
    long phase_sum = 0;
    int phase_count = 0;
    int prev_avg = 0;
    int have_prev = 0;
    int contrast[HISTORY_SIZE] = {0, 0, 0};
    int contrast_count = 0;
    int contrast_index = 0;
    int present = -1;
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
    sample_ms = half_ms / 6;
    if (sample_ms < 5) {
        sample_ms = 5;
    }
    if (sample_ms > 50) {
        sample_ms = 50;
    }

    printf("Deteccion de LED - ADC AIN%d (%s)\n", ADC_AIN, ADC_PIN);
    printf("  ADC        : %s\n", adc_path);
    printf("  LED        : gpio%d, %s\n", LED_GPIO,
           options.led_enabled ? "parpadeando" : "sin --led, la deteccion dara 0");
    printf("  Parpadeo   : %.2f Hz\n\n", options.blink_hz);
    fflush(stdout);

    start_time = monotonic_seconds();
    while (!stop_requested) {
        double now = monotonic_seconds();
        long elapsed_ms = (long)((now - start_time) * 1000.0);
        int blink = (elapsed_ms % (long)period_ms) < (long)half_ms;
        int raw;

        if (blink != last_blink) {
            if (last_blink >= 0 && phase_count > 0) {
                int avg = (int)(phase_sum / phase_count);

                if (have_prev) {
                    int current = last_blink ? (avg - prev_avg) : (prev_avg - avg);

                    contrast[contrast_index] = current;
                    contrast_index = (contrast_index + 1) % HISTORY_SIZE;
                    if (contrast_count < HISTORY_SIZE) {
                        contrast_count++;
                    }

                    if (contrast_count == HISTORY_SIZE) {
                        int new_present;

                        if (evaluate(contrast, &new_present)) {
                            present = new_present;
                            printf("[%8.3f s] %d  (fase=%s avg=%d dif=%+d)\n",
                                   now - start_time, present ? 1 : 0,
                                   last_blink ? "ON" : "OFF", avg, current);
                            fflush(stdout);
                        }
                    }
                }
                prev_avg = avg;
                have_prev = 1;
            }

            phase_sum = 0;
            phase_count = 0;
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
        phase_sum += raw;
        phase_count++;

        sleep_ms(sample_ms);
    }

    if (options.led_enabled) {
        gpio_cleanup(LED_GPIO, exported_by_us);
    }
    return 0;
}
