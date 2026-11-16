#ifndef PHY_SAMPLE_H
#define PHY_SAMPLE_H

int phy_sample_classify(int average, int previous_sample, int on_level, int off_level);
int phy_sample_decode_pair(int previous_sample, int current_sample);

#endif
