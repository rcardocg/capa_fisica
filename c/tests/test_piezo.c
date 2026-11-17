#include "phy_piezo_dsp.h"
#include "phy_sample.h"

#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void expect_int(const char *label, int expected, int actual)
{
    if (expected != actual) {
        fprintf(stderr, "FALLO %s: esperado %d, obtenido %d\n",
                label, expected, actual);
        failures++;
    }
}

static void test_idle_tracks_baseline(void)
{
    phy_piezo_monitor_t monitor;
    int samples[8];
    int index;
    int level;

    for (index = 0; index < 8; index++) {
        samples[index] = 2048;
    }
    phy_piezo_monitor_init(&monitor, 2048, 6);
    level = phy_piezo_monitor_feed(&monitor, samples, 8, 60);
    expect_int("idle nivel", 0, level);
    expect_int("idle baseline", 2048, monitor.baseline);
}

static void test_ac_signal_envelope(void)
{
    phy_piezo_monitor_t monitor;
    int samples[4];

    samples[0] = 1948;
    samples[1] = 2148;
    samples[2] = 2048;
    samples[3] = 2048;
    phy_piezo_monitor_init(&monitor, 2048, 6);
    expect_int("ac nivel", 100, phy_piezo_monitor_feed(&monitor, samples, 4, 60));
    expect_int("ac baseline no deriva", 2048, monitor.baseline);
}

static void test_envelope_detector_signal(void)
{
    phy_piezo_monitor_t monitor;
    int samples[4];
    int level;

    samples[0] = 2200;
    samples[1] = 2210;
    samples[2] = 2195;
    samples[3] = 2205;
    phy_piezo_monitor_init(&monitor, 2048, 6);
    level = phy_piezo_monitor_feed(&monitor, samples, 4, 60);
    expect_int("envolvente nivel", 162, level);
    expect_int("envolvente no deriva", 2048, monitor.baseline);
}

static void test_hysteresis(void)
{
    expect_int("histeresis alto", 1, phy_sample_classify(200, 0, 120, 60));
    expect_int("histeresis bajo", 0, phy_sample_classify(40, 1, 120, 60));
    expect_int("histeresis conserva 1", 1, phy_sample_classify(90, 1, 120, 60));
    expect_int("histeresis conserva 0", 0, phy_sample_classify(90, 0, 120, 60));
}

int main(void)
{
    test_idle_tracks_baseline();
    test_ac_signal_envelope();
    test_envelope_detector_signal();
    test_hysteresis();

    if (failures != 0) {
        fprintf(stderr, "test_piezo: %d fallos\n", failures);
        return 1;
    }
    printf("test_piezo: OK\n");
    return 0;
}
