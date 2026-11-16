#ifndef PHY_ANALOG_H
#define PHY_ANALOG_H

#include <stddef.h>

int phy_analog_find_raw(int ain, char *path, size_t capacity);
int phy_analog_read_raw(const char *path, int *raw);
int phy_analog_read_average(const char *path, int samples, int interval_ms, int *average);

#endif
