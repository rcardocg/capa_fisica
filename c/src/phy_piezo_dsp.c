#include "phy_piezo_dsp.h"

#include <stddef.h>

static int absolute(int value)
{
    return value < 0 ? -value : value;
}

void phy_piezo_monitor_init(phy_piezo_monitor_t *monitor, int baseline,
                            int baseline_shift)
{
    if (monitor == NULL) {
        return;
    }
    monitor->baseline = baseline;
    monitor->baseline_shift = baseline_shift > 0 ? baseline_shift : 6;
    monitor->level = 0;
    monitor->mean = baseline;
    monitor->peak = baseline;
    monitor->trough = baseline;
}

int phy_piezo_monitor_feed(phy_piezo_monitor_t *monitor, const int *samples,
                           int count, int gate_level)
{
    long sum = 0;
    int minimum;
    int maximum;
    int index;
    int positive;
    int negative;

    if (monitor == NULL || samples == NULL || count <= 0) {
        return 0;
    }

    minimum = samples[0];
    maximum = samples[0];
    for (index = 0; index < count; index++) {
        sum += samples[index];
        if (samples[index] < minimum) {
            minimum = samples[index];
        }
        if (samples[index] > maximum) {
            maximum = samples[index];
        }
    }

    monitor->mean = (int)(sum / count);
    monitor->peak = maximum;
    monitor->trough = minimum;

    positive = absolute(maximum - monitor->baseline);
    negative = absolute(monitor->baseline - minimum);
    monitor->level = positive > negative ? positive : negative;

    if (monitor->level <= gate_level) {
        int delta = monitor->mean - monitor->baseline;
        int divisor_value = 1 << monitor->baseline_shift;
        int step = delta / divisor_value;

        if (step == 0 && delta != 0) {
            step = delta > 0 ? 1 : -1;
        }
        monitor->baseline += step;
    }

    return monitor->level;
}
