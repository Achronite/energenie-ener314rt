/* hrmf69_hw.c 14/01/23  Achronite - Hardware SPI driver using spidev supported, fallback to software SPI if unavailable
 *
 * Hope RF RFM69 radio controller low level register interface.
 */

#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include "system.h"
#include "hrfm69.h"
#include "spi.h"
#include "trace.h"
#include "gpio.h"
#include "../achronite/leds.h"

/* globals*/
static volatile bool _spi_hw_driver = false;
static int _spi_hw_fd = 0;      // global

/*---------------------------------------------------------------------------*/
// Initialise radio hardware, SPI and GPIO
// Hardware edition uses spidev, Software edition uses spi.c and gpio

int HRF_spi_init(void){
    int ret;
    uint8_t spiMode = ( SPI_MODE_0 );

    // initialise GPIO using gpiod (NOTE: gpiod is NOT used by the software SPI code, it uses the deprecated WiringPi interface)
    ret = leds_initialise();

    if (ret == 0) {
        // Try using hardware SPI driver first
        _spi_hw_fd = open("/dev/spidev0.1", O_RDWR);    // use 0.1 for CS LOW
        if (_spi_hw_fd < 0)
        {
            _spi_hw_driver = false;
            printf("ener314rt: Cannot open /dev/spidev0.1 - Fallback to Software SPI driver\n");

            // Initialise wiringPi gpio separately for SPI
            TRACE_OUTS("ener314rt: Initialising gpio\n");
            ret = gpio_init();
            if (ret == 0) {
                ret = spi_init_defaults();
            }
        } else {
            _spi_hw_driver = true;
            printf("ener314rt: Hardware driver enabled on /dev/spidev0.1\n");
            // Set SPI mode
            // Open the SPI interface.
            // Default settings:
            //    Mode:           SPI_MODE_0
            //    Speed:          10000000 (10 MHz)
            //    Bits per Word:  8

            //printf("Setting spiMode=%d\n",spiMode);
            ret = ioctl(_spi_hw_fd, SPI_IOC_RD_MODE, &spiMode);

            if (ioctl(_spi_hw_fd, SPI_IOC_WR_MODE, &spiMode) == 0){
                // Get SPI mode
                ret = ioctl(_spi_hw_fd, SPI_IOC_RD_MODE, &spiMode);
    #ifdef TRACE
                printf("Hardware SPI ret=%d,spiMode=%d\n",ret,spiMode);
    #endif

                // Set Word Length    
                uint8_t spiWordLen = 8;
                ret = ioctl(_spi_hw_fd, SPI_IOC_WR_BITS_PER_WORD, &spiWordLen);

                // Set SPI speed
                uint32_t spiSpeed   = 10000000;
                ret = ioctl(_spi_hw_fd, SPI_IOC_WR_MAX_SPEED_HZ, &spiSpeed);
                if (ret != 0){
                    printf("ener314rt: ioctl failed SPI_IOC_WR_MODE\n");
                }
            } else {
                ret = -2;
            }
        }
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
// Private function that perform all communication with radio adaptor after initialisation using the appropriate interface.
// This requires all buffers to be malloced before calling, allowing +1 length for command
// Command must be populated in txbuf[0] before calling
//
// Achronite: Jan 2023

uint8_t _HRF_xfer( uint8_t* txbuf, uint8_t* rxbuf, uint8_t len )
{
    int status = 0;
    if (_spi_hw_driver){
        // hardware driver

        struct spi_ioc_transfer xfer;
        memset(&xfer, 0, sizeof(xfer));

        // Setup transfer params
        xfer.tx_buf        = (uintptr_t)txbuf;
        xfer.rx_buf        = (uintptr_t)rxbuf;
        xfer.delay_usecs   = 0;
        xfer.speed_hz      = 9000000;   // 10MHz does not work on pi5 (so dropped to 9MHz)
        xfer.bits_per_word = 8;
        xfer.len           = len;
        xfer.cs_change     = 0;

        // transmit the register to read - this returns length of transmit
        status = ioctl(_spi_hw_fd, SPI_IOC_MESSAGE(1), &xfer);

    } else {
        // software driver
        uint8_t rxByte = 0;

        // perform xfer
        spi_select();
        for (int i=0; i<len; i++){
            rxByte = spi_byte(txbuf[i]);
            if (rxbuf){
                // assign return if rxbuf defined
                rxbuf[i] = rxByte;
            }
        }
        spi_deselect();

        // assume all sent/received in software mode
        status = len;
    }
    
    return (status);

}

/*---------------------------------------------------------------------------*/
// Write an 8 bit value to a register

void HRF_writereg(uint8_t addr, uint8_t data)
{
    // len = HRF_xfer( NULL, Tx, 2)
    // ( cmd, Tx, 1 )
    int status = 0;
    int len = 2;
    uint8_t txbuf[2];

/* Commented out, rarely use this for debug

    #if defined(FULLTRACE)        
        TRACE_OUTS("HRF_writereg(");
        TRACE_OUTN(addr);
        TRACE_OUTC(',');
        TRACE_OUTN(data);
        TRACE_OUTC(')');
        TRACE_NL();
    #endif
*/

    txbuf[0] = addr | HRF_MASK_WRITE_DATA;
	txbuf[1] = data;

    status = _HRF_xfer( txbuf, NULL, len );

    if (status != len){
        TRACE_OUTS("HRF_writereg(): Failed. status=");
        TRACE_OUTN(status);
        TRACE_NL();
    }
	return;
}


/*---------------------------------------------------------------------------*/
// Read an 8 bit value from a register

uint8_t HRF_readreg(uint8_t addr)
{
    int status = 0;
    int len = 2;

    uint8_t txbuf[len];
    uint8_t rxbuf[len];

    txbuf[0] = addr;
	txbuf[1] = 0;

    status = _HRF_xfer( txbuf, rxbuf, len );

    if (status == len){
        return rxbuf[1];
    } else {
        return status;
    }
}


/*---------------------------------------------------------------------------*/
// Write all bytes in buf to the payload FIFO, in a single burst

void HRF_writefifo_burst(uint8_t* buf, uint8_t len)
{
    int status = 0;

    // allocate a tx buffer, adding 1 to length for the instruction to beginning
    uint8_t *txbuf = (uint8_t*)malloc((len + 1) * sizeof(uint8_t));

    // 1st byte needs to be the instruction
    txbuf[0] = HRF_ADDR_FIFO | HRF_MASK_WRITE_DATA;     // write FIFO
    memcpy( &txbuf[1], buf, len * sizeof(uint8_t)); 

    status = _HRF_xfer( txbuf, NULL, (len + 1) );

    if (status != (len+1)){
        TRACE_OUTS("HRF_writefifo_burst(): Failed. status=");
        TRACE_OUTN(status);
        TRACE_NL();
    }
	
    free(txbuf);
	return;
}


/*---------------------------------------------------------------------------*/
// Read bytes from FIFO in burst mode.
// Never reads more than buflen bytes
// First received byte is the count of remaining bytes
// That byte is also returned in the user buffer.
// Note the user buffer can be > FIFO_MAX, but there is no flow control
// in the HRF driver yet, so you might get an underflow error if data is read
// quicker than it comes in on-air. You might get an overflow error if
// data comes in quicker than it is read.


HRF_RESULT HRF_readfifo_burst_cbp(uint8_t* buf, uint8_t buflen)
{
    int status = 0;

    // add 1 to length to cater for instruction to beginning
    uint8_t *txbuf = (uint8_t*)calloc((buflen + 1), sizeof(uint8_t));

    // 1st byte needs to be the command
	txbuf[0] = HRF_ADDR_FIFO;

    // Initial Read with length=2 to get the content (CBP) length
    status = _HRF_xfer( txbuf, buf, 2 );

    if (status==2){

        uint8_t payload_len = buf[1];

        if (payload_len > 5 && payload_len <= buflen){
            // content length within range, read the rest of the payload
            status = _HRF_xfer( txbuf, buf, payload_len+1 );

            if (status==(payload_len+1)){
                // full payload done, set PAYLOAD length in 1st byte of buf
                buf[0] = payload_len;
            } else {
                buf[0] = 0;
            }
        }
    }

    // Always clear any extraneous bytes from FIFO buffer   
    //HRF_clear_fifo();

    #ifdef FULLTRACE
        TRACE_OUTS("HRF_readfifo_burst_cbp() len=");
        TRACE_OUTN(status);
        TRACE_OUTS(", data:");
        for (int i=0; i<=buflen; i++ ){
            TRACE_OUTN(buf[i]);
            TRACE_OUTC(':');
        }
        TRACE_NL();
    #endif

    free(txbuf);

    return HRF_RESULT_OK;
}



/*---------------------------------------------------------------------------*/
// Read bytes from FIFO in burst mode.
// Tries to read exactly buflen bytes
/* unused
HRF_RESULT HRF_readfifo_burst_len(uint8_t* buf, uint8_t buflen)
{
    // read in one block up to expected length
    TRACE_OUTS("HRF_readfifo_burst()");
    int status = 0;
    struct spi_ioc_transfer xfer;
    memset(&xfer, 0, sizeof(xfer));

    uint8_t *txbuf = (uint8_t*)calloc((buflen + 1), sizeof(uint8_t));
    uint8_t *rxbuf = (uint8_t*)calloc((buflen + 1), sizeof(uint8_t));

    // 1st byte needs to be the address
	txbuf[0] = HRF_ADDR_FIFO; 
     
    status = _HRF_xfer( txbuf, rxbuf, buflen+1 );

    if (status != (buflen+1)){
        TRACE_OUTS("HRF_readfifo_burst(): Failed. status=");
        TRACE_OUTN(status);
        TRACE_NL();
    } else {
        // copy result ignoring 1st byte
        memcpy( buf, &rxbuf[1], buflen * sizeof(uint8_t));
    }
	
    free(txbuf);
    free(rxbuf);
    
    return HRF_RESULT_OK;
}
*/



/*---------------------------------------------------------------------------*/
// Check to see if a register matches a specific value or not

HRF_RESULT HRF_checkreg(uint8_t addr, uint8_t mask, uint8_t value)
{
    uint8_t regval = HRF_readreg(addr);
    if ((regval & mask) == value)
    {
        return HRF_RESULT_OK_TRUE;
    }
    return HRF_RESULT_OK_FALSE;
}


/*---------------------------------------------------------------------------*/
// Poll a register until it meets some criteria

void HRF_pollreg(uint8_t addr, uint8_t mask, uint8_t value)
{
    while (! HRF_checkreg(addr, mask, value))
    {
      // busy wait
      //TODO: No timeout or error recovery? Can cause permanent lockup
      #if defined(FULLTRACE)
        TRACE_OUTC('P');
      #endif

      // added the best sleep function for linux OS, transmit usually takes 40-60ms, sleep 20ms = 20,000,000ns
      nanosleep((const struct timespec[]){{0, 20000000L}}, NULL);
    }
}


/*---------------------------------------------------------------------------*/
// Clear any data in the HRF payload FIFO, by reading until empty

void HRF_clear_fifo(void)
{
    //TODO: max fifolen is 66, should bail after that to prevent lockup
    //especially if radio crashed and SPI always returns stuck flag bit
    while ((HRF_readreg(HRF_ADDR_IRQFLAGS2) & HRF_MASK_FIFONOTEMPTY) == HRF_MASK_FIFONOTEMPTY)
    {
        HRF_readreg(HRF_ADDR_FIFO);
    }
}


/***** END OF FILE *****/

