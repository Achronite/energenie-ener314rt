/* init_loop.h
 *
 */

#ifndef _INIT_H
#define _INIT_H

#include <stdint.h>
#include <pthread.h>


// Radio constants (from radio.c)
#define MAX_FIFO_BUFFER   66

// Variable types
enum deviceTypes{DT_CONTROL = 1, DT_MONITOR, DT_LEARN};

// Rx Message
struct RADIO_MSG {
    time_t t;
    unsigned char msg[MAX_FIFO_BUFFER];
};
#define RX_MSGS 5

// function prototypes
int init_ener314rt(int lock);
void close_ener314rt(void);
int lock_ener314rt(void);
int unlock_ener314rt(void);
int empty_radio_Rx_buffer(enum deviceTypes rxMode);
int pop_RxMsg(struct RADIO_MSG *rxMsg);
int get_RxMsg(int msgNum, struct RADIO_MSG *rxMsg);
int send_radio_msg(unsigned char mod, unsigned char *payload, unsigned char len, unsigned char times);

#endif

//globals
//extern pthread_mutex_t radio_mutex;

/***** END OF FILE *****/
