/*
** Abstract all functions that interact with GPIO directly to ensure forwards compatibility and deprecate use of gpio.h
** this includes the LEDs and RESET pin in spidev mode
**
** These functions use the linux kernel driver for GPIO (gpiod)
**
** NOTE: spi is abstracted separately, and uses gpio functions if using the software driver
**
** Achronite: November 2023
*/

#include <gpiod.h>      // sudo apt-get install gpiod libgpiod-dev
#include <stdio.h>
#include <unistd.h>
#include "leds.h"

// Globals
struct gpiod_chip *chip;
struct gpiod_line *lineTxLED;  // Red LED
struct gpiod_line *lineRxLED;  // Green LED
struct gpiod_line *lineReset;  // Reset Pin

int leds_initialise()
{
    const char *chipname = "gpiochip0";
    int ret = 0;

    // Open GPIO chip
    chip = gpiod_chip_open_by_name(chipname);
    if (!chip)
    {
		printf("leds_initialise(): Open gpiod chip failed\n");
		return -1;
    }

    // Initialise GPIO lines
    lineRxLED = gpiod_chip_get_line(chip, LED_RX);
    if (!lineRxLED) {
        perror("leds_initialise(): gpiod_get_line LED_RX failed\n");
        return -2;
    }
    
    lineTxLED = gpiod_chip_get_line(chip, LED_TX);
    if (!lineTxLED) {
        perror("leds_initialise(): gpiod_get_line LED_TX failed\n");
        return -3;
    }
    
    lineReset = gpiod_chip_get_line(chip, RESET);
    if (!lineTxLED) {
        perror("leds_initialise(): gpiod_get_line RESET failed\n");
        return -4;
    }

    // Set all GPIOs to output
    ret = gpiod_line_request_output(lineRxLED, CONSUMER, 0);
    if (ret < 0) {
        perror("leds_initialise(): gpiod_line_req_output Rx failed");
        gpiod_line_release(lineRxLED);
        return ret;
    }

    ret = gpiod_line_request_output(lineTxLED, CONSUMER, 0);
    if (ret < 0) {
        perror("leds_initialise(): gpiod_line_req_output Tx failed");
        gpiod_line_release(lineTxLED);
        return ret;
    }

    ret = gpiod_line_request_output(lineReset, CONSUMER, 0);
    if (ret < 0) {
        perror("leds_initialise(): gpiod_line_req_output RESET failed");
        gpiod_line_release(lineReset);
        return ret;
    }

    return ret;
}

int leds_Tx()
{
    // illuminates the Tx LED and switches off the Rx LED
    int ret = 0;

    ret = gpiod_line_set_value(lineTxLED, 1);
	if (ret >= 0) {
        ret = gpiod_line_set_value(lineRxLED, 0);
	    if (ret < 0) {
            perror("leds_Tx(): Set line value Rx failed\n");
        }
    } else {
        perror("leds_Tx(): Set line value Tx failed\n");
    }

    return ret;
}

int leds_Rx()
{
    // illuminates the Rx LED and switches off the Tx LED
    int ret = 0;

    ret = gpiod_line_set_value(lineTxLED, 0);
	if (ret >= 0) {
        ret = gpiod_line_set_value(lineRxLED, 1);
	    if (ret < 0) {
            perror("leds_Rx(): Set line value Rx failed\n");
        }
    } else {
        perror("leds_Rx(): Set line value Tx failed\n");
    }

    return ret;    
}

int leds_standby()
{
    // switches off the Rx and Tx LEDs
    int ret = 0;

    ret = gpiod_line_set_value(lineTxLED, 0);
	if (ret >= 0) {
        ret = gpiod_line_set_value(lineRxLED, 0);
	    if (ret < 0) {
            perror("leds_standby(): Set line value Rx failed\n");
        }
    } else {
        perror("leds_standby(): Set line value Tx failed\n");
    }

    return ret;    
}

int leds_reset_board()
{
    // resets the board, flashing the LEDs
    int ret = 0;

    ret = gpiod_line_set_value(lineTxLED, 1);
	if (ret >= 0) {
        ret = gpiod_line_set_value(lineRxLED, 1);
	    if (ret >= 0) {
            ret = gpiod_line_set_value(lineReset, 1);
            if (ret >= 0) {
                usleep(10000);
                // Hold reset pin HIGH to cause board to reinitialise
                ret = gpiod_line_set_value(lineReset, 1);
                if (ret >= 0) {
                    usleep(150);
                    // probs safe to assume everything else will run OK, as we have done them all once already

                    // pull RESET pin LOW after short delay
                    ret = gpiod_line_set_value(lineReset, 0);

                    // switch off LEDs
                    ret = gpiod_line_set_value(lineRxLED, 0);
                    ret = gpiod_line_set_value(lineTxLED, 0);
                } else {
                    perror("leds_reset_board()): Set line value reset 0 failed\n");
                }
            } else {
                perror("leds_reset_board()): Set line value reset 1 failed\n");
            }
        } else {
            perror("leds_reset_board()): Set line value Rx failed\n");
        }
    } else {
        perror("leds_Rx(): Set line value Tx failed\n");
    }

    return ret;
    
}

void leds_close()
{
    // Close GPIO chip (gpiod)
    if (chip)
    {
        gpiod_chip_close(chip);
    }
}
