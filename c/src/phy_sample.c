#include "phy_sample.h"

int phy_sample_classify(int average, int previous_sample, int on_level, int off_level)
{
    if (average >= on_level) {
        return 1;
    }
    if (average <= off_level) {
        return 0;
    }
    return previous_sample;
}

int phy_sample_decode_pair(int previous_sample, int current_sample)
{
    if (previous_sample == 0 && current_sample == 0) {
        return 0;
    }
    if (previous_sample == 0 && current_sample == 1) {
        return 1;
    }
    if (previous_sample == 1 && current_sample == 0) {
        return 0;
    }
    return 1;
}
