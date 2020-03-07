/* gpio.h  D.J.Whale  8/07/2014
 *
 * @Achronite - added return values to remove exit(s), Feb 2020
 * 
 * A very simple interface to the GPIO port on the Raspberry Pi.
 */

#ifndef GPIO_H
#define GPIO_H

#include "system.h"

extern const uint8_t gpio_sim; /* 0=> not simulated */

/***** FUNCTION PROTOTYPES *****/

int     gpio_init(void);
void    gpio_setin(uint8_t g);
void    gpio_setout(uint8_t g);
void    gpio_high(uint8_t g);
void    gpio_low(uint8_t g);
void    gpio_write(uint8_t g, uint8_t v);
uint8_t gpio_read(uint8_t g);
void    gpio_finished(void);

/**** Bad return codes from radio / gpio / spi ****/
#define ERR_MMAP -2
#define ERR_RADIO_MIN -3
#define ERR_RADIO_MAX -4
#define ERR_CPHA1 -5
#define ERR_GPIO_DEVICE -6

#endif

/***** END OF FILE *****/

