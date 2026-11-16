#ifndef PHY_GPIO_H
#define PHY_GPIO_H

typedef struct {
    int gpio;
    int exported_by_us;
    int is_output;
} phy_gpio_t;

int phy_gpio_open_output(phy_gpio_t *pin, int gpio);
int phy_gpio_open_input(phy_gpio_t *pin, int gpio);
int phy_gpio_write(phy_gpio_t *pin, int value);
int phy_gpio_read(phy_gpio_t *pin, int *value);
void phy_gpio_close(phy_gpio_t *pin);

#endif
