#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include "lock_radio.h"
#include "openThings.h"
#include "../energenie/radio.h"
#include "../energenie/hrfm69.h"
#include "../energenie/trace.h"

/*
** C module addition to energenie code to perform the receiving and locking for the Energenie ENER314-RT board
** It also provides the common functions for mutex locking, and radio initialisation
**
** Author: Phil Grainger - @Achronite, March 2019 - Dec 2010
**
** 10 Jan 2020 v0.3.1 Call init function if not already done so when attempting to lock radio
**
*/

/* declare radio lock for multi-threading */
pthread_mutexattr_t attr;
pthread_mutex_t radio_mutex;

static bool initialised = false;

enum deviceTypes deviceType = DT_CONTROL; // types of devices in use to control loop behaviour

static struct RADIO_MSG RxMsgs[RX_MSGS];
static int pRxMsgHead = 0; // pointer to the next slot to load a Rx msg into in our Rx Msg FIFO
static int pRxMsgTail = 0; // pointer to the next msg to read from Rx Msg FIFO, if head=tail the buffer is empty

/* init_ener314rt() - initialise radio adaptor in a multi-threaded environment
**
** mutex locking is performed here AND RETAINED if lock=true
**
** @Achronite - March 2019
** 
*/
int init_ener314rt(int lock)
{
    int ret = 0;

    if (!initialised)
    {
        //initialise radio
        TRACE_OUTS("init_ener314(");
        TRACE_OUTN(lock);
        TRACE_OUTS("): Initialising\n");

        // set mutex type to not deadlock if relocking the same mutex
        if ((ret = pthread_mutexattr_init(&attr)) == 0)
        {
            if ((ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK)) == 0)
            {
                // TODO: use the mutex attr!
                if ((ret = pthread_mutex_init(&radio_mutex, NULL)) != 0)
                {
                    // mutex failure
                    TRACE_OUTS("init_ener314(): mutex init failed err=");
                    TRACE_OUTN(ret);
                    TRACE_NL();

                    if (lock && (ret == EBUSY))
                    {
                        // another process has the lock; lets spin on the lock assuming that another thread has initialised the radio & mutex
                        TRACE_OUTS("init_ener314(): initialised already, await lock\n");
                        return pthread_mutex_lock(&radio_mutex);
                    }
                }

                // mutex initialised; lock it
                if ((ret = pthread_mutex_lock(&radio_mutex)) == 0)
                {
                    // thread safe, set initialised
                    TRACE_OUTS("init_ener314(): mutex created & locked\n");
                    if ((ret = radio_init()) == 0)
                    {
                        // place radio in known modulation and mode - FSK:Standby
                        initialised = true;
                        radio_setmode(RADIO_MODULATION_FSK,HRF_MODE_STANDBY);
                    }

                    if (!lock)
                    {
                        // unlock mutex if not required to be retained
                        TRACE_OUTS("init_ener314(): mutex unlocked\n");
                        pthread_mutex_unlock(&radio_mutex);
                    }
                }
            }
        }
    }
    return ret;
}

/*
** lock_ener314rt() - lock the mutex to ensure we have exclusive access to radio adaptor in multithreaded environment
**
** Copes with conditions where we already have the lock
*/
int lock_ener314rt(void)
{
    int ret = -1;

    if (initialised)
    {
        // lock radio now
#ifdef LOCKTRACE
        TRACE_OUTS("[L");
        TRACE_OUTN((int)pthread_self());
        TRACE_OUTS("]");
#endif

        ret = pthread_mutex_lock(&radio_mutex);
        //printf("-%d-", (int)pthread_self());
        //fflush(stdout);

        // cater for if we already have the lock
        if (ret == EDEADLK)
        {
            // mutex already locked, this is fine!
            ret = 0;
        }
    }
    else
    {
        // radio not initialised, do it now
        TRACE_OUTS("lock_ener314(): Radio not initialised, calling init_ener314rt()\n");
        ret = init_ener314rt(true);
    }

    return ret;
}

int unlock_ener314rt(void)
{
    int ret = 0;
    //unlock mutex
#ifdef LOCKTRACE
    TRACE_OUTS("[");
    TRACE_OUTN((int)pthread_self());
    TRACE_OUTS("U]");
#endif
    ret = pthread_mutex_unlock(&radio_mutex);
    return ret;
}

/*
** elegant shutdown of radio adaptor
*/
void close_ener314rt(void)
{
    //Elegant shutdown of ener314rt
    TRACE_OUTS("close_ener314(): called\n");
    if (lock_ener314rt() == 0)
    {
        // we have the lock, do all the tidying
        radio_finished();
        initialised = false;
        deviceType = DT_CONTROL; // set back to control mode only
        pthread_mutex_destroy(&radio_mutex);
        TRACE_OUTS("close_ener314(): done\n");
    }
    else
    {
        TRACE_OUTS("close_ener314(): couldnt get lock\n");
    }
}

/*
** empty_radio_Rx_buffer() - empties the radio receive buffer of any messages into RxMsgs as quickly as possible
**
** Need to mutex lock before calling this function
** Leave the radio in receive mode if monitoring, as this could have been the first time we have been called
**
** returns the # of messages read
**
** TODO: Buffer is currently cyclic and destructive, we could lose messages, but I've made the assumption we always need
**       the latest messages
**
*/
int empty_radio_Rx_buffer(enum deviceTypes rxMode)
{
    int i, recs = 0;

    // Put us into monitor mode as soon as we know about it
    if (rxMode == DT_MONITOR)
        deviceType = DT_MONITOR;

    // only empty buffer if we need to
    if (deviceType == DT_MONITOR || rxMode == DT_LEARN)
    {
        // only receive data if we are in monitor mode
        // Set FSK mode receive for OpenThings devices (Energenie OOK devices dont generally transmit!)
        radio_setmode(RADIO_MODULATION_FSK, HRF_MODE_RECEIVER);

        i = 0;
        // do we have any messages waiting?
        while (radio_is_receive_waiting() && (i++ < RX_MSGS))
        {
            if (radio_get_payload_cbp(RxMsgs[pRxMsgHead].msg, MAX_FIFO_BUFFER) == RADIO_RESULT_OK)
            {
                recs++;
                // TODO: Only store valid OpenThings messages?

                // record message timestamp
                RxMsgs[pRxMsgHead].t = time(0);
                TRACE_OUTC(64);

                // wrap round buffer for next Rx
                if (++pRxMsgHead == RX_MSGS)
                    pRxMsgHead = 0;
            }
            else
            {
                TRACE_OUTS("empty_radio_Rx_buffer(): invalid OT payload\n");
                break;
            }
        }
    }
    return recs;
};

/*
** pop_msg() - returns next unread message from Rx queue
**
** returns -1 if no messages, or # of msg remaining in FIFO
**
** TODO: Implement mutex on rxMsg buffer
*/
int pop_RxMsg(struct RADIO_MSG *rxMsg)
{
    int ret = -1;

    //printf("<%d-%d>", pRxMsgHead, pRxMsgTail);
    if (pRxMsgHead != pRxMsgTail)
    {
        memcpy(rxMsg->msg, RxMsgs[pRxMsgTail].msg, sizeof(rxMsg->msg));

        // null out read message
        memset(RxMsgs[pRxMsgTail].msg, 0, MAX_FIFO_BUFFER);
        rxMsg->t = RxMsgs[pRxMsgTail].t;

        // move tail to next msg in buffer
        if (++pRxMsgTail == RX_MSGS)
            pRxMsgTail = 0;

        if (pRxMsgHead >= pRxMsgTail)
        {
            ret = pRxMsgHead - pRxMsgTail;
        }
        else
        {
            ret = pRxMsgHead + RX_MSGS - pRxMsgTail;
        }
    }

    //printf("pop_RxMsg(%d): %d\n", ret, (int)rxMsg->t);

    return ret;
}

/*
** get_Rxmsg() - returns message msgNum from Rx queue
**
** This performs a simple memory copy, and does not perform ANY checking that message is valid
**
*/
int get_RxMsg(int msgNum, struct RADIO_MSG *rxMsg)
{
    if (msgNum < 0 || msgNum >= RX_MSGS)
    {
        // out of range
        return -1;
    }

    memcpy(rxMsg->msg, RxMsgs[msgNum].msg, sizeof(rxMsg->msg));
    rxMsg->t = RxMsgs[msgNum].t;

    //printf("get_RxMsg(%d): %d\n", msgNum, (int)rxMsg->t);

    return (int)rxMsg->t;
}

/*
** send_radio_msg() - transmits a given payload a number of times
**
** Use this function to perform a raw transmit with full locking and mode switching
** Minimal checking is performed in this function
*/
int send_radio_msg(unsigned char mod, unsigned char *payload, unsigned char len, unsigned char times)
{
    int ret = 0;

    #if defined(FULLTRACE)
        TRACE_OUTS("radio_mod_transmit(): called\n");
    #endif
    
    if (lock_ener314rt() == 0)
    {
        radio_mod_transmit(mod, payload, len, times);
        ret = unlock_ener314rt();
    }
    else
    {
        TRACE_OUTS("radio_mod_transmit(): couldn't get lock\n");
        ret = -1;
    }
    return ret;
}