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

#ifndef LEDS_H
#define LEDS_H
#define	CONSUMER	"ENER314-RT-gpiod"
#endif

/* GPIO assignments for Raspberry Pi using BCM numbering */
#define RESET 25
// GREEN used for RX, RED used for TX
#define LED_RX 27 // (not B rev1)
#define LED_TX 22

// Function prototypes

int leds_initialise();
int leds_Tx();
int leds_Rx();
int leds_standby();
int leds_reset_board();
void leds_close();