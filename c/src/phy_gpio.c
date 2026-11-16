#define _POSIX_C_SOURCE 200809L

#include "phy_gpio.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

static int read_int(const char *path, int *value)
{
    char buffer[32];
    char *end = NULL;
    long parsed;
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
    parsed = strtol(buffer, &end, 10);
    if (errno != 0 || end == buffer) {
        return -1;
    }
    *value = parsed != 0 ? 1 : 0;
    return 0;
}

static int export_gpio(int gpio, int *exported_by_us)
{
    char path[PATH_MAX];
    char number[32];

    *exported_by_us = 0;
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", gpio);
    if (access(path, F_OK) == 0) {
        return 0;
    }
    snprintf(number, sizeof(number), "%d", gpio);
    if (write_text("/sys/class/gpio/export", number) != 0) {
        return -1;
    }
    *exported_by_us = 1;
    sleep_ms(100);
    return 0;
}

static int set_direction(int gpio, const char *direction)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    return write_text(path, direction);
}

static int open_pin(phy_gpio_t *pin, int gpio, int output)
{
    if (pin == NULL || gpio < 0) {
        return -1;
    }
    pin->gpio = gpio;
    pin->is_output = output;
    pin->exported_by_us = 0;
    if (export_gpio(gpio, &pin->exported_by_us) != 0) {
        pin->exported_by_us = 0;
        return -1;
    }
    if (set_direction(gpio, output ? "out" : "in") != 0) {
        phy_gpio_close(pin);
        return -1;
    }
    return 0;
}

int phy_gpio_open_output(phy_gpio_t *pin, int gpio)
{
    return open_pin(pin, gpio, 1);
}

int phy_gpio_open_input(phy_gpio_t *pin, int gpio)
{
    return open_pin(pin, gpio, 0);
}

int phy_gpio_write(phy_gpio_t *pin, int value)
{
    char path[PATH_MAX];

    if (pin == NULL || !pin->is_output) {
        errno = EINVAL;
        return -1;
    }
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin->gpio);
    return write_text(path, value ? "1" : "0");
}

int phy_gpio_read(phy_gpio_t *pin, int *value)
{
    char path[PATH_MAX];

    if (pin == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin->gpio);
    return read_int(path, value);
}

void phy_gpio_close(phy_gpio_t *pin)
{
    char number[32];
    char path[PATH_MAX];

    if (pin == NULL || pin->gpio < 0) {
        return;
    }
    if (pin->is_output) {
        (void)phy_gpio_write(pin, 0);
    }
    if (pin->exported_by_us) {
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", pin->gpio);
        if (access(path, F_OK) == 0) {
            snprintf(number, sizeof(number), "%d", pin->gpio);
            (void)write_text("/sys/class/gpio/unexport", number);
        }
    }
    pin->gpio = -1;
    pin->exported_by_us = 0;
}
