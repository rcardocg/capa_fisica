#define _POSIX_C_SOURCE 200809L

#include "phy_analog.h"

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

int phy_analog_find_raw(int ain, char *path, size_t capacity)
{
    int device;
    char candidate[PATH_MAX];

    if (path == NULL || capacity == 0 || ain < 0) {
        return 0;
    }

    for (device = 0; device < 10; device++) {
        snprintf(candidate, sizeof(candidate),
                 "/sys/bus/iio/devices/iio:device%d/in_voltage%d_raw", device, ain);
        if (access(candidate, F_OK) == 0) {
            snprintf(path, capacity, "%s", candidate);
            return 1;
        }
    }

    snprintf(candidate, sizeof(candidate),
             "/sys/devices/platform/ocp/44e0d000.tscadc/44e0d000.tscadc:adc/in_voltage%d_raw",
             ain);
    if (access(candidate, F_OK) == 0) {
        snprintf(path, capacity, "%s", candidate);
        return 1;
    }
    return 0;
}

int phy_analog_read_raw(const char *path, int *raw)
{
    char buffer[64];
    char *end = NULL;
    long value;
    int descriptor;
    ssize_t length;

    if (path == NULL || raw == NULL) {
        return -1;
    }
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

int phy_analog_read_average(const char *path, int samples, int interval_ms, int *average)
{
    long sum = 0;
    int valid = 0;
    int index;

    if (path == NULL || average == NULL || samples <= 0) {
        return -1;
    }
    for (index = 0; index < samples; index++) {
        int raw;

        if (phy_analog_read_raw(path, &raw) == 0) {
            sum += raw;
            valid++;
        }
        if (index + 1 < samples) {
            sleep_ms(interval_ms);
        }
    }
    if (valid == 0) {
        return -1;
    }
    *average = (int)(sum / valid);
    return 0;
}
