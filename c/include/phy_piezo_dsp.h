#ifndef PHY_PIEZO_DSP_H
#define PHY_PIEZO_DSP_H

typedef struct {
    int baseline;
    int baseline_shift;
    int level;
    int mean;
    int peak;
    int trough;
} phy_piezo_monitor_t;

void phy_piezo_monitor_init(phy_piezo_monitor_t *monitor, int baseline,
                            int baseline_shift);
int phy_piezo_monitor_feed(phy_piezo_monitor_t *monitor, const int *samples,
                           int count, int gate_level);

#endif
