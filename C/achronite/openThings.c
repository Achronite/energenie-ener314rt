#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include "openThings.h"
#include "lock_radio.h"
#include "../energenie/radio.h"
#include "../energenie/hrfm69.h"
#include "../energenie/trace.h"
#include "../energenie/delay.h"

/*
** C module addition to energenie code to simplify the FSK OpenThings interaction with the Energenie ENER314-RT
** by minimising the number of calls required to interact with C radio device.
**
** Author: Phil Grainger - @Achronite, March 2019 - October 2021
*/

// OpenThings FSK paramters (known)  [{ParamName, paramId}]
// I've moved the likely ones to the top for speed, and included in .c file to prevent compiler warnings

static struct OT_PARAM OTparams[NUM_OT_PARAMS] = {
    {"UNKNOWN", 0x00},
    {"MOTION_DETECTOR", 0x6D},
    {"FREQUENCY", 0x66},
    {"REAL_POWER", 0x70},
    {"REACTIVE_POWER", 0x71},
    {"SWITCH_STATE", 0x73},
    {"TEMPERATURE", 0x74},
    {"VOLTAGE", 0x76},
    {"DIAGNOSTICS", 0x26},
    {"ALARM", 0x21},
    {"THERMOSTAT_MODE", 0x2A}, // for Thermostat
    {"RELAY_POLARITY",0x2B},
    {"DEBUG_OUTPUT", 0x2D},
    {"HUMID_OFFSET",0x3A},
    {"TEMP_OFFSET",0x3D},
    {"IDENTIFY", 0x3F},
    {"SOURCE_SELECTOR", 0x40}, // write only
    {"WATER_DETECTOR", 0x41},
    {"GLASS_BREAKAGE", 0x42},
    {"CLOSURES", 0x43},
    {"DOOR_BELL", 0x44},
    {"ENERGY", 0x45},
    {"FALL_SENSOR", 0x46},
    {"GAS_VOLUME", 0x47},
    {"AIR_PRESSURE", 0x48},
    {"ILLUMINANCE", 0x49},
    {"TARGET_TEMP", 0x4B}, // for Thermostat
    {"LEVEL", 0x4C},
    {"RAINFALL", 0x4D},
    {"BUTTON", 0x4F}, // MiHome Click
    {"APPARENT_POWER", 0x50},
    {"POWER_FACTOR", 0x51},
    {"REPORT_PERIOD", 0x52},    // aka REPORTING_INTERVAL
    {"SMOKE_DETECTOR", 0x53},
    {"TIME_AND_DATE", 0x54},
    {"VIBRATION", 0x56},
    {"WATER_VOLUME", 0x57},
    {"WIND_SPEED", 0x58},
    {"WAKEUP", 0x59}, // for Thermostat
    {"GAS_PRESSURE", 0x61},
    {"BATTERY_LEVEL", 0x62},
    {"CO_DETECTOR", 0x63},
    {"DOOR_SENSOR", 0x64},
    {"EMERGENCY", 0x65},
    {"GAS_FLOW_RATE", 0x67},
    {"REL_HUMIDITY", 0x68},
    {"CURRENT", 0x69},
    {"JOIN", 0x6A},
    {"RF_QUALITY", 0x6B},
    {"LIGHT_LEVEL", 0x6C},
    {"OCCUPANCY", 0x6F},
    {"ROTATION_SPEED", 0x72},
    {"WATER_FLOW_RATE", 0x77},
    {"WATER_PRESSURE", 0x78},
    {"HYSTERESIS",0x7E},
    {"TEST", 0xAA}};


// OpenThings FSK products (known)  [{mfrId, productId, control (0=no, 1=yes, 2=cached), product}]
static struct OT_PRODUCT OTproducts[NUM_OT_PRODUCTS] = {
    {4, 0x00, 1, "Unknown"},
    {4, 0x01, 0, "Monitor Plug"},
    {4, 0x02, 1, "Smart Plug+"},
    {4, 0x03, 2, "Radiator Valve"},
    {4, 0x05, 0, "House Monitor"},
    {4, 0x0C, 0, "Motion Sensor"},
    {4, 0x0D, 0, "Open Sensor"},
    {4, 0x12, 2, "Thermostat"},
    {4, 0x13, 0, "Click"}
};

// Globals - yuck
unsigned short g_ran;
struct OT_DEVICE g_OTdevices[MAX_DEVICES]; // TODO: should maybe make this dynamic!
static volatile int g_NumDevices = 0;      // number of auto-discovered OpenThings devices
static volatile int g_CachedCmds = 0;      // number of eTRV devices with commands waiting to be sent to them (controls Rx loop behaviour)
static volatile int g_PreCachedCmds = 0;   // for caching commands before device discovered

// declare and initialise cached count lock for multi-threading
pthread_mutex_t cachedcount_mutex = PTHREAD_MUTEX_INITIALIZER;

// private function declarations
static void _update_cachedcmd_count(int delta, bool isCached);
static int _openThings_build_msg(unsigned char iProductId, unsigned int iDeviceId, unsigned char iCommand, float fData, unsigned char *radio_msg);
static int _openThings_build_record(unsigned char iCommand, float fData, unsigned char *sRecord);

/*
** calculateCRC()- Calculate an OpenThings CRC
** Code converted from python module by @whaleygeek
*/
unsigned short calculateCRC(unsigned char *msg, unsigned int length)
{
    unsigned char ch, bit;
    unsigned short rem = 0, i; // uint16_t

    for (i = 0; i < length; i++)
    {
        ch = msg[i];
        // printf("%d=%d\n",i,ch);
        rem = rem ^ (ch << 8);
        for (bit = 0; bit < 8; bit++)
        {
            if ((rem & (1 << 15)) != 0)
            {
                // bit is set
                rem = ((rem << 1) ^ 0x1021);
            }
            else
            {
                // bit is clear
                rem = (rem << 1);
            }
        }
    }
    return rem;
}

/*
** cryptByte()- en/decrypt a single Byte of an OpenThings message
** Code converted from python module by @whaleygeek
*/
unsigned char cryptByte(unsigned char data)
{
    unsigned char i;

    for (i = 0; i < 5; i++)
    {
        if ((g_ran & 0x01) != 0)
        {
            // bit0 set
            g_ran = ((g_ran >> 1) ^ 62965);
        }
        else
        {
            // bit0 clear
            g_ran = g_ran >> 1;
        }
    }
    return (g_ran ^ data ^ 90);
}

/*
** cryptMsg() - en/decrypt an OpenThings message (destructive)
**
** Code converted from python module by @whaleygeek
*/
void cryptMsg(unsigned char pid, unsigned short pip, unsigned char *msg, unsigned int length)
{
    unsigned char i;

    // old whaleygeek
    g_ran = (((pid & 0xFF) << 8) ^ pip);

    // new from gpbenton
    // g_ran = ((((unsigned short)pid) << 8) ^ pip);

    for (i = 0; i < length; i++)
    {
        msg[i] = cryptByte(msg[i]);
    }
}

/*
** OTtypelen() - return the number of bits used to encode a specific OpenThings record data type
*/
char OTtypelen(unsigned char OTtype)
{
    char bits = 0;

    switch (OTtype)
    {
    case OT_UINT4:
        bits = 4;
        break;
    case OT_UINT8:
    case OT_SINT8:
        bits = 8;
        break;
    case OT_UINT12:
        bits = 12;
        break;
    case OT_UINT16:
    case OT_SINT16:
        bits = 16;
        break;
    case OT_UINT20:
        bits = 20;
        break;
    case OT_UINT24:
    case OT_SINT24:
        bits = 24;
    }
    return bits;
}

/* finds the product code in the OTproducts array and returns index
 */
int openThings_getProductIndex(const char id)
{
    for (size_t i = 1; i < NUM_OT_PRODUCTS; i++)
    {
        if (OTproducts[i].productId == id)
            return i;
    }
    return 0; // unknown
}

/* finds the param code in the OTparams array and returns index
 */
int openThings_getParamIndex(const char id)
{
    for (size_t i = 1; i < NUM_OT_PARAMS; i++)
    {
        if (OTparams[i].paramId == id)
            return i;
    }
    return 0; // unknown
}

/* openThings_getDeviceIndex() - finds the id in the g_OTdevices array and returns index if it exists, otherwise return -1
 */
int openThings_getDeviceIndex(unsigned int id)
{
    for (int i = 0; i < g_NumDevices; i++)
    {
        if (g_OTdevices[i].deviceId == id)
        {
            return i;
        }
    }
    return -1;
}

/*
** openThings_devicePut() - add device to deviceList if it is not already there, return the index
*/
int openThings_devicePut(unsigned int iDeviceId, unsigned char mfrId, unsigned char productId, bool joined)
{
    int OTpi, OTdi;

    OTdi = openThings_getDeviceIndex(iDeviceId);
    if (OTdi < 0)
    {
        // TODO: malloc device (get rid of MAX_DEVICES)

        // TODO: add mutex to g_OTdevices/g_NumDevices

        // new device
        OTdi = g_NumDevices;
        g_OTdevices[OTdi].mfrId = mfrId;
        g_OTdevices[OTdi].productId = productId;
        g_OTdevices[OTdi].deviceId = iDeviceId;
        g_OTdevices[OTdi].joined = joined;

        // add product characteristics
        OTpi = openThings_getProductIndex(productId);
        g_OTdevices[OTdi].control = OTproducts[OTpi].control;
        strcpy(g_OTdevices[OTdi].product, OTproducts[OTpi].product);

        // add cached data structure for devices that needed it
        if (g_OTdevices[OTdi].control == 2)
        {
            TRACE_OUTS("openThings_devicePut() adding cache cmd struct.\n");
            g_OTdevices[OTdi].cache = malloc(sizeof(struct CACHED_CMD));
            if (g_OTdevices[OTdi].cache != NULL)
            {
                // malloc OK, set defaults for cached device
                g_OTdevices[OTdi].cache->radio_msg[0] = '\0';
                g_OTdevices[OTdi].cache->retries = 0;
                g_OTdevices[OTdi].cache->command = 0;
                g_OTdevices[OTdi].cache->active = true; // set device active here, as it will be overriden in PreCached mode
            }
        }

        // add extra structure if it is an eTRV
        if (productId == PRODUCTID_MIHO013)
        {
            TRACE_OUTS("openThings_devicePut() adding trv struct.\n");
            g_OTdevices[OTdi].trv = malloc(sizeof(struct TRV_DEVICE));
            if (g_OTdevices[OTdi].trv != NULL)
            {
                // malloc OK, set defaults for trv
                g_OTdevices[OTdi].trv->valve = UNKNOWN;
                g_OTdevices[OTdi].trv->voltageDate = 0;
                g_OTdevices[OTdi].trv->diagnosticDate = 0;
                g_OTdevices[OTdi].trv->valveDate = 0;
                g_OTdevices[OTdi].trv->diagnostics = 0;
                g_OTdevices[OTdi].trv->voltage = 0;
                g_OTdevices[OTdi].trv->targetC = 0;
                g_OTdevices[OTdi].trv->errors = false;
                g_OTdevices[OTdi].trv->lowPowerMode = false;
                memset(g_OTdevices[OTdi].trv->errString, 0, MAX_ERRSTR+1);
            }
            g_OTdevices[OTdi].thermostat = NULL;
        }
        // add extra structure if a thermostat
        else if (productId == PRODUCTID_MIHO069)
        {
            TRACE_OUTS("openThings_devicePut() adding thermostat struct.\n");
            g_OTdevices[OTdi].thermostat = malloc(sizeof(struct STAT_DEVICE));
            if (g_OTdevices[OTdi].thermostat != NULL)
            {
                // malloc OK, set defaults
                g_OTdevices[OTdi].thermostat->mode = GATEWAY;
                g_OTdevices[OTdi].thermostat->telemetryDate = 0;
            }
            g_OTdevices[OTdi].trv = NULL;
        }
        else
        {
            // this isnt special, so we dont need extra structs
            g_OTdevices[OTdi].trv = NULL;
            g_OTdevices[OTdi].thermostat = NULL;
        }

#if defined(TRACE)
        TRACE_OUTS("openThings_devicePut() device added: ");
        TRACE_OUTN(OTdi);
        TRACE_OUTC(':');
        TRACE_OUTN(iDeviceId);
        TRACE_NL();
#endif
        g_NumDevices++;
    }
    // else
    // {
    //     printf("openThings_devicePut() device %d already exist\n", iDeviceId);
    // }
    return OTdi;
}

/*
** openThings_decode()
** ===================
** Decode an OpenThings payload
**
** The OpenThings messages are comprised of 3 parts:
**  Header  - msgLength, manufacturerId, productId, encryptionPIP, and deviceId
**  Records - The body of the message, in this case a single command to switch the state
**  Footer  - CRC
**
** Functions performed here include:
**   Decoding the received OpenThings message
**   Sending any outstanding commands to an eTRV ASAP
*/
int openThings_decode(unsigned char *payload, unsigned char *mfrId, unsigned char *productId, unsigned int *iDeviceId, struct OTrecord recs[])
{
    unsigned char length, i, j, param, rlen;
    unsigned short pip, crc, crca;
    int result = 0;
    int record = 0;
    int index = 0;
    // float f;

    // struct openThingsHeader sOTHeader;

    /* max buffer is 66, so what is reasonable for records array?  bytes left after CRC (2), Header (8) = 56
     * each record has 2 bytes header and a variable length payload of up to 15 (0xF) bytes based on type,
     * which looking at the spec is max 24bits (excluding FLOAT), so 3-17 bytes / record; so max would be 56/3=18 records
     */

    // reproduce issue #33
    // memcpy(payload, (unsigned char []){28,4,2,20,252,97,146,180,94,192,160,222,89,15,199,175,253,53,51,182,70,231,121,124,227,205,125,90,134},29);
    // memcpy(payload, (unsigned char []){13,4,2,91,7,48,104,172,179,88,183,44,66,242,0},14);
    //memcpy(payload, (unsigned char []){14,4,18,10,131,19,68,104,131,132,112,212,159,153,11,84,72,10,166},15);

    length = payload[0];

    // A good indication this is an OpenThings msg, is to check the length first, abort if too long or short
    if (length > MAX_FIFO_BUFFER || length < 10)
    {
        // Not OT Message, invalid length
        return -1;
    }

    // DECODE HEADER
    *mfrId = payload[1];
    *productId = payload[2];
    pip = (unsigned short)((payload[OTH_INDEX_PIP] << 8) | payload[OTH_INDEX_PIP + 1]);

    // decode encrypted body (destructive - watch for length errors!)
    cryptMsg(CRYPT_PID, pip, &payload[5], (length - 4));
    *iDeviceId = (payload[5] << 16) + (payload[6] << 8) + payload[7];

    // CHECK CRC from last 2 bytes of message
    crca = (payload[length - 1] << 8) + payload[length];
    crc = calculateCRC(&payload[5], (length - 6));

    if (crc != crca)
    {
// CRC does not match
#ifdef FULLTRACE
        TRACE_OUTS("openThings_decode() len=");
        TRACE_OUTN(length);
        TRACE_OUTS(", decoded data:");
        for (int i = 0; i <= length; i++)
        {
            TRACE_OUTN(payload[i]);
            TRACE_OUTC(':');
        }
        printf(" crc:%d, crca:%d, deviceId=%u - CRC FAILED\n", crc, crca, *iDeviceId);
#endif
        return -2;
    }
    else
    {

        // CRC OK

        // find discovered device and check for outstanding cached cmds for it
        // check the global first (quick) to see if we have any cached cmds outstanding

        //*******************************************************************************************************HERE*******************
        if (g_CachedCmds > 0 || g_PreCachedCmds > 0)
        {
            index = openThings_getDeviceIndex(*iDeviceId);
            if (index >= 0 && g_OTdevices[index].control == 2 && g_OTdevices[index].cache->retries > 0)
            {
                // Only send commands on wakeup of thermostat
                if (*productId != PRODUCTID_MIHO069 || payload[8] == OTP_WAKEUP) {
                    openThings_cache_send(index);
                }
                
            }
        }
        // DECODE RECORDS
        i = 8; // start at the 1st record

        while ((i < (length - 2)) && (payload[i] != 0) && (record < OT_MAX_RECS))
        {
            // reset values
            // memset(recs[record].retChar, '\0', 15);
            result = 0;

            // PARAM
            param = payload[i++];
            //recs[record].wr = ((param & 0x80) == 0x80);
            //recs[record].paramId = param & 0x7F;
            recs[record].paramId = param;
            recs[record].cmd = (param & 0x80);

            // lookup the parameter name in the known parameters table (commands are converted to responses)
            int paramIndex = openThings_getParamIndex(recs[record].paramId & 0x7F);
            if (paramIndex != 0)
            {
                if (recs[record].cmd){
                    // record is a command from the gateway or another instance 
                    sprintf(recs[record].paramName, "_%s",  OTparams[paramIndex].paramName);
                } else {
                    strncpy(recs[record].paramName, OTparams[paramIndex].paramName, OT_PARAM_NAME_LEN);
                }
            }
            else
            {
                // unknown parameter
                sprintf(recs[record].paramName, "UNKNOWN_0x%2x", recs[record].paramId);
            }

            // TYPE/LEN
            recs[record].typeId = payload[i] & 0xF0;
            rlen = payload[i++] & 0x0F;

            if (rlen > 0)
            {
                // str can cause probs clear it
                memset(recs[record].retChar, 0, 15);

                // set MSB always to reduce loops below
                result = payload[i];

                // In C, it is not great at returning different types for a function; so we are just going to have to code it here rather than be a modular coder :(
                switch (recs[record].typeId)
                {
                case OT_CHAR:
                    // memcpy is faster
                    memcpy(recs[record].retChar, &payload[i], rlen);
                    recs[record].typeIndex = OTR_CHAR;
                    break;
                case OT_UINT:
                    for (j = 1; j < rlen; j++)
                    {
                        result <<= 8;
                        result += payload[i + j];
                    }
                    recs[record].typeIndex = OTR_INT;
                    break;
                case OT_UINT4:
                case OT_UINT8:
                case OT_UINT12:
                case OT_UINT16:
                case OT_UINT20:
                case OT_UINT24:
                    for (j = 1; j < rlen; j++)
                    {
                        result <<= 8;
                        result += payload[i + j];
                    }
                    // adjust BP
                    recs[record].typeIndex = OTR_FLOAT;
                    break;
                case OT_SINT:
                case OT_SINT8:
                case OT_SINT16:
                case OT_SINT24:
                    for (j = 1; j < rlen; j++)
                    {
                        // printf("%d,", payload[i + j]);
                        result <<= 8;
                        result += payload[i + j];
                    } // turn to signed int based on high bit of MSB, 2's comp is 1's comp plus 1
                    if ((payload[i] & 0x80) == 0x80)
                    {
                        // negative
                        result = -(((!result) & ((2 ^ (length * 8)) - 1)) + 1);
                    }
                    recs[record].typeIndex = OTR_INT;
                    break;
                case OT_FLOAT:
                    // TODO (@whaleygeek didnt do this either!)
                    recs[record].typeIndex = -1;
                    break;
                default:
                    // TODO - are there other values?
                    recs[record].typeIndex = -2;
                }

                // always store the integer result in the record
                recs[record].retInt = result;
                // Binary point adjustment (float)
                recs[record].retFloat = (float)result / pow(2, OTtypelen(recs[record].typeId));
            }
            else
            {
#ifdef TRACE
                printf("openThings_decode(): No data for %s received for device %u:%d\n", recs[record].paramName, *productId, *iDeviceId);
#endif
                recs[record].retInt = 0;
            }

            // move arrays on
            i += rlen;
            record++;
        }
    }

    // printf("openThings_decode(): returning %d (i=%d)\n",record,i);
    //  return the number of records
    return record;
}

/*
** openThings_switch()
** ===================
** Send a switch signal to a 'Control and Monitor' RF FSK OpenThings based Energenie smart device
** Currently this covers the 'HiHome Adaptor Plus' and 'MiHome Heating' TRV
**
** The OpenThings messages are comprised of 3 parts:
**  Header  - msgLength, manufacturerId, productId, encryptionPIP, and deviceId
**  Records - The body of the message, in this case a single command to switch the state
**  Footer  - CRC
**
** Functions performed include:
**    initialising the radio and setting the modulation
**    encoding of the device and switch status
**    formatting and encoding the OpenThings FSK radio request
**    sending the radio request via the ENER314-RT RaspberryPi adaptor
*/
int openThings_switch(unsigned char iProductId, unsigned int iDeviceId, unsigned char bSwitchState, unsigned char xmits)
{
    char ret = 0;
    unsigned short crc, pip;
    unsigned char radio_msg[OTS_MSGLEN] = {OTS_MSGLEN - 1, ENERGENIE_MFRID, PRODUCTID_MIHO005, OT_DEFAULT_PIP, OT_DEFAULT_DEVICEID, OTC_SWITCH_OFF, 0x00, 0x00};

#if defined(TRACE)
    printf("openThings_switch: productId=%d, deviceId=%d, state=%d\n", iProductId, iDeviceId, bSwitchState);
#endif
    /*
    ** Stage 1: Build the message to send
    */

    /* Stage 1a: OpenThings HEADER
     */
    // productId (usually 2 for MIHO005)
    radio_msg[OTH_INDEX_PRODUCTID] = iProductId;

    /*
    ** Stage 1b: OpenThings RECORDS (Commands)
    */
    // deviceId
    radio_msg[OTH_INDEX_DEVICEID] = (iDeviceId >> 16) & 0xFF;    // MSB
    radio_msg[OTH_INDEX_DEVICEID + 1] = (iDeviceId >> 8) & 0xFF; // MID
    radio_msg[OTH_INDEX_DEVICEID + 2] = iDeviceId & 0xFF;        // LSB

    // pip (random)
    radio_msg[OTH_INDEX_PIP] = rand();
    radio_msg[OTH_INDEX_PIP + 1] = rand();
    pip = (unsigned short)((radio_msg[OTH_INDEX_PIP] << 8) | radio_msg[OTH_INDEX_PIP + 1]);

    if (bSwitchState)
    {
        // We already have the switch off command in the message, just override the switch value to on
        radio_msg[OT_INDEX_R1_VALUE] = 1;
    }

    /*
    ** Stage 1c: OpenThings FOOTER (CRC)
    */
    crc = calculateCRC(&radio_msg[5], (OTS_MSGLEN - 7));
    radio_msg[OTS_MSGLEN - 2] = ((crc >> 8) & 0xFF); // MSB
    radio_msg[OTS_MSGLEN - 1] = (crc & 0xFF);        // LSB

#if defined(TRACE)
    TRACE_OUTS("switch tx payload (unencrypted):\n");
    for (int i = 0; i < OTS_MSGLEN; i++)
    {
        TRACE_OUTN(radio_msg[i]);
        TRACE_OUTC(',');
    }
    TRACE_NL();
#endif

    // Stage 1d: encrypt body part of message, using the stored pip
    cryptMsg(CRYPT_PID, pip, &radio_msg[5], (OTS_MSGLEN - 5));

    /*
    ** Stage 2: Empty Rx buffer if required
    */

    // mutex access radio adaptor to set mode
    if ((ret = lock_ener314rt()) != 0)
    {
        TRACE_FAIL("openthings_switch(): error getting lock\n");
        return -1;
    }
    else
    {
        // #32 - Do not return the record count
        empty_radio_Rx_buffer(DT_CONTROL);
        // printf("openThings_switch(%d): Rx_Buffer ", recs);

        /*
        ** Stage 3: Transmit via radio adaptor, using mutex to block the radio
        */
        // Transmit encoded payload 26ms per payload * xmits
        radio_mod_transmit(RADIO_MODULATION_FSK, radio_msg, OTS_MSGLEN, xmits);

        // release mutex lock
        unlock_ener314rt();
    }

    return ret;
}

/*
** _openThings_build_msg()
** ===================
** Creates a fully-formed radio message to be sent to an FSK OpenThings based device
** Message is not sent here
**
** TODO: Make this the only msg builder and sender, caching if device requires it
**
** Currently this has been tested with 'HiHome Adaptor Plus' and 'MiHome Heating' TRV
** Other OpenThings devices should work
**
** The OpenThings messages are comprised of 3 parts:
**  Header  - msgLength, manufacturerId, productId, encryptionPIP, and deviceId
**  Records - The body of the message, in this case a single command to switch the state
**  Footer  - CRC
**
** Functions performed include:
**  NO initialising the radio and setting the modulation
**     encoding of the device and command
**     formatting and encoding the OpenThings FSK radio request
**  NO sending the radio request via the ENER314-RT RaspberryPi adaptor
**     returning built message
*/
int _openThings_build_msg(unsigned char iProductId, unsigned int iDeviceId, unsigned char iCommand, float fData, unsigned char *radio_msg)
{
    int ret = 0, reclen=0;
    unsigned short crc, pip;
    // unsigned char radio_msg[MAX_R1_MSGLEN] = {0x00, ENERGENIE_MFRID, PRODUCTID_MIHO005, OT_DEFAULT_PIP, OT_DEFAULT_DEVICEID, 0x00, 0x00, 0x00, 0x00};
    unsigned char msglen = 0;

#if defined(TRACE)
    printf("openThings_build_msg: productId=%d, deviceId=%d, data=%g, cmd=(%d) ", iProductId, iDeviceId, fData, iCommand);
#endif

    // call new function to build the record
    // TODO: Allow for multiple commands/records in a single request

    reclen = _openThings_build_record(iCommand, fData, &radio_msg[OT_INDEX_R1_CMD]);

    if (reclen > 0)
    {
        /*
        ** Stage 1: Build the message to send
        */

        /* Stage 1a: OpenThings HEADER
         */
        // message length
        msglen = MIN_R1_MSGLEN + reclen - 2;
        radio_msg[0] = msglen - 1;

        // product
        radio_msg[OTH_INDEX_MFRID] = ENERGENIE_MFRID;
        radio_msg[OTH_INDEX_PRODUCTID] = iProductId;

        // pip random
        radio_msg[OTH_INDEX_PIP] = rand();
        radio_msg[OTH_INDEX_PIP + 1] = rand();
        pip = (unsigned short)((radio_msg[OTH_INDEX_PIP] << 8) | radio_msg[OTH_INDEX_PIP + 1]);

        // deviceId
        radio_msg[OTH_INDEX_DEVICEID] = (iDeviceId >> 16) & 0xFF;    // MSB
        radio_msg[OTH_INDEX_DEVICEID + 1] = (iDeviceId >> 8) & 0xFF; // MID
        radio_msg[OTH_INDEX_DEVICEID + 2] = iDeviceId & 0xFF;        // LSB

        /*
        ** Stage 1b: Records
        **
        ** Performed above by _openThings_build_record
        */

        /*
        ** Stage 1c: OpenThings FOOTER (CRC)
        */
        crc = calculateCRC(&radio_msg[5], (msglen - 7));
        radio_msg[msglen - 2] = ((crc >> 8) & 0xFF); // MSB
        radio_msg[msglen - 1] = (crc & 0xFF);        // LSB

#if defined(FULLTRACE)
        TRACE_OUTS("Built payload (unencrypted): ");
        for (int i = 0; i < msglen; i++)
        {
            TRACE_OUTN(radio_msg[i]);
            TRACE_OUTC(',');
        }
        TRACE_NL();
#endif

        // Stage 1d: encrypt body part of message
        cryptMsg(CRYPT_PID, pip, &radio_msg[5], (msglen - 5));
    }
    else
    {
        // unknown command, ignore
#if defined(TRACE)
        printf("UNKNOWN Command=%d\n", iCommand);
#endif
        ret = -1;
    }

    return ret;
}

/*
** openThings_cmd() - New in v0.4
** ================
** Send a command to be sent to a 'Control and Monitor' RF FSK OpenThings based Energenie smart device
** This is designed for devices that are continously listening for commands
**
** NOTE: This deliberately does not double check the device type as 'control' (1), so be careful this is the right function to call
*/
int openThings_cmd(unsigned char iProductId, unsigned int iDeviceId, unsigned char command, float fData, unsigned char xmits)
{
    int ret = 0;
    unsigned char radio_msg[MAX_R1_MSGLEN] = {0};
    unsigned char msglen;

#if defined(TRACE)
    printf("openThings_cmd(): deviceId=%d, cmd=%d, data=%g\n", iDeviceId, command, fData);
#endif

    /*
    ** We are just going to send any command, regardless of if the device is the deviceList or Not
    */

    // build full radio message
    ret = _openThings_build_msg(iProductId, iDeviceId, command, fData, radio_msg);

    if (ret == 0)
    {
        msglen = radio_msg[0] + 1; // use the length already calculated and stored
        TRACE_OUTS("openThings_cmd(): sending...\n");

        if ((ret = lock_ener314rt()) == 0)
        {
            radio_mod_transmit(RADIO_MODULATION_FSK, radio_msg, msglen, xmits);
            unlock_ener314rt();

#if defined(TRACE)
            printf("openThings_cmd(): sent\n");
#endif
        }
        else
        {
            TRACE_FAIL("openThings_cmd(): ERROR getting lock");
        }
    }

    return ret;
}

/*
** openThings_cache_cmd()
** ===================
** Cache a command to be sent to a 'Control and Monitor' RF FSK OpenThings based Energenie smart device
** This is designed for devices that have a small receive window such as the 'MiHome Heating' TRV
**
** Build the full message here, as Rx window is quite small for eTRV
**
** Sending a command of 0 will clear the existing cached command
**
*/
int openThings_cache_cmd(unsigned char iProductId, unsigned int iDeviceId, unsigned char command, float fData, unsigned char retries)
{
    int ret = 0, index, OTpi;
    unsigned char radio_msg[MAX_R1_MSGLEN] = {0};

#if defined(TRACE)
    printf("openThings_cache_cmd(): productId=%d, deviceId=%d, cmd=%d, data=%g, retries=%d\n", iProductId, iDeviceId, command, fData, retries);
#endif

    /*
    ** Device must be type 2 to accept cached commands, these devices have an extra .cache structure to store the command
    ** but they MUST be in the deviceList before proceeding
    ** (i.e. we have had an Rx a msg from it)
    */
    index = openThings_getDeviceIndex(iDeviceId);
    if (index < 0)
    {
        // unknown device
        if (command == 0)
        {
            // Unknown device with an invalid command - exit!
            TRACE_OUTS("openThings_cache_cmd() WARNING: Cannot cancel a command for an unknown device\n");
            return -1;
        }
        else
        {
            // The device is not in the deviceList, add a placeholder for the device IF IT IS A CACHABLE DEVICE
            OTpi = openThings_getProductIndex(iProductId);
            if (OTproducts[OTpi].control == 2)
            {
                // cachable device
                index = openThings_devicePut(iDeviceId, ENERGENIE_MFRID, iProductId, false);
                g_OTdevices[index].cache->active = false; // indicates an unknown device
            }
            else
            {
                // This is not a message cachable device, abort
                TRACE_OUTS("openThings_cache_cmd() ERROR: Cannot cache commands for this type of unknown device\n");
                return -4;
            }
        }
    }

    // Check that the device is actually cachable to prevent mem errs (#18)
    if (g_OTdevices[index].control == 2)
    {
        // allow cancel of existing cached command (Issue #27)
        if (command == 0)
        {
            // cancel the existing cached command for this device
            if (g_OTdevices[index].cache->command > 0)
            {
                g_OTdevices[index].cache->command = 0;
                g_OTdevices[index].cache->retries = 0;

                // Decrement CachedCmd Count
                _update_cachedcmd_count(-1, g_OTdevices[index].cache->active);
            };
        }
        else
        {
            // Build full radio message
            ret = _openThings_build_msg(g_OTdevices[index].productId, iDeviceId, command, fData, radio_msg);

            if (ret == 0)
            {
                // store message against the Device array, only 1 cached command is supported for each device
                // this now overwrites any existing command
                // TODO: add mutex
                if (g_OTdevices[index].cache->retries <= 0)
                {
                    // No existing command, so need to update cached/pre-cached command count
                    _update_cachedcmd_count(1, g_OTdevices[index].cache->active);
                } else {
                    TRACE_OUTS("openThings_cache_cmd(): WARNING: existing cached command replaced\n");
                }

                memcpy(g_OTdevices[index].cache->radio_msg, radio_msg, MAX_R1_MSGLEN);
                g_OTdevices[index].cache->command = command;
                g_OTdevices[index].cache->data = fData;
                g_OTdevices[index].cache->retries = retries; // Rx window is really small, so retry the Tx this number of times

                // Store any output only variables in state for eTRV
                if (g_OTdevices[index].productId == PRODUCTID_MIHO013)
                {
                    switch (command)
                    {
                    case OTCP_TARGET_TEMP:
                        g_OTdevices[index].trv->targetC = fData;
                        break;
                    case OTCP_SWITCH_STATE:
                        g_OTdevices[index].trv->valve = (int)fData;
                    }
                }

            }
        }
    }
    else
    {
        // This is not a message cachable device, abort
        TRACE_OUTS("openThings_cache_cmd() ERROR: Cannot cache commands for this type of device\n");
        ret = -4;
    }

    return ret;
}

/*
** openThings_receive()
** =======
** Receive a single FSK OpenThings message.  This function has 2 modes:
**   - if timeout > 0 then the function will wait 'timeout' ms or until a valid message is received
**   - if timeout = 0 then the function will return immediately, even if there is no valid message received
**
** This node is designed for all 'monitor' & 'control & monitor' nodes, including the 'HiHome Adaptor Plus' and MiHome Heating'
**
** An OpenThings message is comprised of 3 parts:
**  Header  - msgLength, manufacturerId, productId, encryptionPIP, and deviceId
**  Records - The body of the message, which can contain multiple parameters (records) returned
**  Footer  - CRC
**
** Functions performed include:
**   - mutex locking radio adaptor during radio operations
**   - Setting radio to receive mode
**   - receiving data via the ENER314-RT device
**   - formatting and decoding the OpenThings FSK radio responses
**   - auto add any devices to device list, responding to join requests if applicable
**   - If a cached command is outstanding for a device that only has a small receive window (e.g. eTRV), send the command
**   - returning JSON for the received msg OR returning '{"deviceId": 0}' if no msg available
**
** TODO (optimisations):
*/
int openThings_receive(char *OTmsg, unsigned int buflen, unsigned int timeout)
{
    // int ret = 0;
    // uint8_t buf[MAX_FIFO_BUFFER];
    struct OTrecord OTrecs[OT_MAX_RECS];
    unsigned char mfrId, productId;
    unsigned int iDeviceId;
    int records, i, msgsInRxBuf;
    char OTrecord[200];
    struct RADIO_MSG rxMsg;
    bool joined = false;
    ;
    int OTdi;
    struct timeval startTime, currentTime, diffTime;
    unsigned int diff = 0;

    // printf("openthings_receive(): called, buflen=%d\n", buflen);

    // record startTime for timeout
    if (timeout > 0)
    {
        gettimeofday(&startTime, NULL);
    }
    else
    {
        diff = 0;
    }

    // set default message if no message available
    strcpy(OTmsg, "{\"deviceId\": 0}");

    // 2 nested loops here, the plan is to wait until we have a valid message or the 'timeout' is reached:
    //  - the 1st loop empties the radio buffer
    //  - the end loops until buffer empty or we have a valid OT msg
    do
    {
        // Clear data
        records = -1;
        iDeviceId = 0;

        /*
        ** Stage 1 - empty the Rx buffer on the radio device (with locking)
        */
        if ((i = lock_ener314rt()) == 0)
        {
            i = empty_radio_Rx_buffer(DT_MONITOR);
            unlock_ener314rt();
        }
        else
        {
            // probably been asked to close quit loop
            return -3;
        }

        /*
        ** Stage 2 - decode and process next message in RxMsgs buffer
        */
        // printf("<%d-%d>",pRxMsgHead, pRxMsgTail);

        // loop2 - until we have read a valid OTmsg OR the RxMsg buffer is empty
        do
        {
            if ((msgsInRxBuf = pop_RxMsg(&rxMsg)) >= 0)
            {
                // Rx message avaiable in buffer
                // printf("openThings_receive(): msg popped, ts=%d\n", (int)rxMsg.t);
                records = openThings_decode(rxMsg.msg, &mfrId, &productId, &iDeviceId, OTrecs);

                // printf("openThings_decode() returned %d records for deviceId=%d\n",records, iDeviceId);
                if (records > 0)
                {
                    // build response JSON
                    sprintf(OTmsg, "{\"deviceId\":%d,\"mfrId\":%d,\"productId\":%d,\"timestamp\":%d", iDeviceId, mfrId, productId, (int)rxMsg.t);
#if defined(FULLTRACE)
                    TRACE_OUTS("openThings_receive(): hdr: ");
                    TRACE_OUTS(OTmsg);
                    TRACE_NL();
#endif
                    // add records
                    for (i = 0; i < records; i++)
                    {
#if defined(FULLTRACE)
                        TRACE_OUTS("openThings_receive(): rec:");
                        TRACE_OUTN(i);
                        sprintf(OTrecord, " {\"name\":\"%s\",\"id\":%d(%s),\"datatype\":%d,\"str\":\"%s\",\"int\":%d,\"float\":%f}\n", OTrecs[i].paramName, OTrecs[i].paramId, OTrecs[i].cmd?"command":"status", OTrecs[i].typeIndex, OTrecs[i].retChar, OTrecs[i].retInt, OTrecs[i].retFloat);
                        TRACE_OUTS(OTrecord);
#endif
                        switch (OTrecs[i].typeIndex)
                        {
                        case OTR_CHAR: // CHAR
                            sprintf(OTrecord, ",\"%s\":\"%s\"", OTrecs[i].paramName, OTrecs[i].retChar);
                            break;
                        case OTR_INT:
                            sprintf(OTrecord, ",\"%s\":%d", OTrecs[i].paramName, OTrecs[i].retInt);

                            // Special record processing
                            switch (OTrecs[i].paramId)
                            {
                            case OTP_JOIN: // JOIN_ACK
                            case OTCP_JOIN:
                                // We seem to have stumbled upon an instruction to join outside of discovery loop, may as well autojoin the device
                                TRACE_OUTS("openThings_receive(): New device found, sending ACK: deviceId:");
                                TRACE_OUTN(iDeviceId);
                                TRACE_NL();
                                openThings_joinACK(productId, iDeviceId, 20);
                                joined = true;
                                break;
                            case OTP_TEMPERATURE: // TEMPERATURE
                                // Seems that TEMPERATURE (OTP_TEMPERATURE) received as type OTR_INT=1, and it should be OTR_FLOAT=2 from the eTRV, so override and return a float instead
                                sprintf(OTrecord, ",\"%s\":%.1f", OTrecs[i].paramName, OTrecs[i].retFloat);
                                break;
                            }
                            break;
                        case OTR_FLOAT:
                            sprintf(OTrecord, ",\"%s\":%f", OTrecs[i].paramName, OTrecs[i].retFloat);
                            break;
                        case 0:  // No data
                            sprintf(OTrecord, ",\"%s\":0", OTrecs[i].paramName);
                            if (OTrecs[i].paramId == OTCP_JOIN){
                                // We seem to have stumbled upon an instruction to join outside of discovery loop, may as well autojoin the device
                                TRACE_OUTS("openThings_receive(): New device found, sending ACK: deviceId:");
                                TRACE_OUTN(iDeviceId);
                                TRACE_NL();
                                openThings_joinACK(productId, iDeviceId, 10);
                                joined = true;
                                break;                                
                            }
                            break;
                        default:
                            // The type is unknown or not set, assume INT (for now)
#if defined(TRACE)
                            printf("openThings_receive(): WARNING type:%d unknown assuming INT. str:%s,int:%d,float:%f\n", OTrecs[i].typeIndex, OTrecs[i].retChar, OTrecs[i].retInt, OTrecs[i].retFloat);
#endif
                            sprintf(OTrecord, ",\"%s\":%d", OTrecs[i].paramName, OTrecs[i].retInt);
                        }

                        // add OT record to returned msg
                        strcat(OTmsg, OTrecord);
                    }

                    // Add to deviceList
                    OTdi = openThings_devicePut(iDeviceId, mfrId, productId, joined);

                    // Perform any device specific processing
                    switch (productId)
                    {
                    case PRODUCTID_MIHO013: // eTRV
                        // Update eTRV data and append stored info, only one record is ever returned
                        eTRV_update(OTdi, OTrecs[0], rxMsg.t);
                        // Add static params to returned message, this can result in DIAGNOSTICS flag being sent twice, but node copes with that OK
                        eTRV_get_status(OTdi, OTmsg, buflen);
                        break;

                    case PRODUCTID_MIHO069: // thermostat
                        if (g_OTdevices[OTdi].cache != NULL)
                        {
                            // Check if we have a cached command and has it been succesfully processed
                            if (g_OTdevices[OTdi].cache->command != 0)
                            {
                                // Anything othere than a WAKEUP commands show that are a command has been received (and processed if applicable)
                                if (OTrecs[0].paramId != OTP_WAKEUP)
                                {
                                    // telemetry received, make a note of the thermostat mode for replay (ie it has changed)
                                    for (i = 0; i < records; i++)
                                    {
                                        // printf("openThings_receive(): %d command=%d paramId=%d\n", i, g_OTdevices[OTdi].cache->command, OTrecs[i].paramId);
                                        if (OTrecs[i].paramId == OTP_THERMOSTAT_MODE)
                                        {
                                            g_OTdevices[OTdi].thermostat->mode = (unsigned int)OTrecs[i].retInt;

#ifdef TRACE
                                            printf("openThings_receive(): Thermostat mode %d stored, saving for auto-messaging telemetry\n",g_OTdevices[OTdi].thermostat->mode);
#endif

                                        }
                                    }
                                    // Cached command has been processed, stop retrying Tx
                                    // NOTE: This should preserve any button presses made on the device as it will ignore the last command
                                    // TODO: add mutex

                                    // Add the processed command for the parameters that are never returned by the Thermostat
                                    switch (g_OTdevices[OTdi].cache->command){
                                        case OTCP_HYSTERESIS:
                                        case OTCP_HUMID_OFFSET:
                                        case OTCP_RELAY_POLARITY:
                                        case OTCP_SET_THERMOSTAT_MODE:
                                        case OTCP_TEMP_OFFSET:
                                            // Assume (as we could have a gateway) that a non-returned command was processed
                                            // Return the value processed

                                            // lookup the parameter name in the known parameters table (commands are converted to responses)
                                            i = openThings_getParamIndex(g_OTdevices[OTdi].cache->command & 0x7F);                                              
                                            if (i != 0)
                                            {
#ifdef TRACE
                                                printf("openThings_receive(): rec:+ command %s (%d) assumed processed\n",OTparams[i].paramName,g_OTdevices[OTdi].cache->command);
#endif
                                                sprintf(OTrecord, ",\"%s\":%g",OTparams[i].paramName, g_OTdevices[OTdi].cache->data);
                                                strcat(OTmsg, OTrecord);
                                            }
                                    }

                                    g_OTdevices[OTdi].cache->command = 0;
                                    g_OTdevices[OTdi].cache->retries = 0;
                                    _update_cachedcmd_count(-1, g_OTdevices[OTdi].cache->active);
                                    g_OTdevices[OTdi].thermostat->telemetryDate = rxMsg.t; // store last successful telemetry data
#ifdef TRACE
                                    printf("openThings_receive(): stored time %ld %ld\n", g_OTdevices[OTdi].thermostat->telemetryDate, rxMsg.t);
#endif


                                }
                            }
                            else
                            {
                                // No outstanding cached commands...
                                // Create one for the next thermostat WAKEUP if telemetry data is old to allow for periodic auto-reporting for the thermostat
                                // NOTE: We must have already processed a THERMOSTAT_MODE command for this to be enabled

                                if (g_OTdevices[OTdi].thermostat != NULL &&
                                    g_OTdevices[OTdi].thermostat->mode != GATEWAY &&
                                    OTrecs[0].paramId == OTP_WAKEUP)
                                {
                                    // We also need to wait some time before sending a cached command to preserve battery life on thermostat
                                    if ((g_OTdevices[OTdi].thermostat->telemetryDate + (time_t)THERMOSTAT_AUTO_TELEMETRY_TIME) < rxMsg.t)
                                    {
                                        // Sufficient time has passed
#ifdef TRACE
                                        printf("openThings_receive(): %ld + %d < %ld\n", g_OTdevices[OTdi].thermostat->telemetryDate, THERMOSTAT_AUTO_TELEMETRY_TIME, rxMsg.t);
                                        printf("openThings_receive(): adding cached command for auto-reporting thermostat_mode=%d\n", g_OTdevices[OTdi].thermostat->mode);
#endif
                                        // ret = openThings_cmd(productId, iDeviceId, OTCP_SET_THERMOSTAT_MODE, g_OTdevices[OTdi].thermostat->mode, 1);
                                        openThings_cache_cmd(productId, iDeviceId, OTCP_SET_THERMOSTAT_MODE, g_OTdevices[OTdi].thermostat->mode, 3);
                                    }
                                }
                            }

                            // return cached command status (even if retries is 0)
                            sprintf(OTrecord, ",\"command\":%d,\"retries\":%d",
                                    g_OTdevices[OTdi].cache->command,
                                    g_OTdevices[OTdi].cache->retries);
                            strcat(OTmsg, OTrecord);
                        }
                    }

                    // close record array
                    strcat(OTmsg, "}");

                    TRACE_OUTS("openThings_receive(): Returning: ");
                    TRACE_OUTS(OTmsg);
                    TRACE_NL();

                    // we have a message, return
                    return records;
                }
                else
                {
                    // Message read from the buffer was not a valid OpenThings message, loop immediately to get the next msg from buffer
                    TRACE_OUTS("openThings_receive(): Non-OT message, openThings_decode() returned ");
                    TRACE_OUTN(records);
                    TRACE_NL();
                }
            }  // pop
            else
            {
                // no messages remaining in the buffer
                // bufferEmpty = true;
            }
        } while (msgsInRxBuf > 0); // loop until the RxMsg buffer is empty - NOTE return quits loops early above anyways

        if (timeout > 0)
        {
            // Rx buffer is empty, sleep a bit before emptying again
            gettimeofday(&currentTime, NULL);
            timersub(&currentTime, &startTime, &diffTime);
            diff = (diffTime.tv_sec * 1000) + diffTime.tv_usec;

            // sleep a very small bit if we are in WaitForMsg mode for eTRVs only, these have an Rx window of 200ms
            if (diff < timeout)
            {
                if (g_CachedCmds > 0)
                {
                    usleep(25000); // 25ms
                }
                else
                {
                    // sleep reduced to 0.5s from 5s (issue #14 - events missed)
                    usleep(500000);
                }
            }
        }
    } while (timeout > diff);

    return records;
}

/*
** openThings_deviceList() - return list of known openThings devices
**
** deviceList is built up automatically by
**  - receive an OT payload
**  - learn a new device
**  - or by a manual poll if empty
**
** v0.4.1: Changed return type to a dynamically allocated string, which should be free()d by caller
*/
char *openThings_deviceList(bool scan)
{
    int i;
    char deviceStr[100];

    TRACE_OUTS("openthings_deviceList(): called\n");

    if (g_NumDevices == 0 || scan)
    {
        // If we dont have any learnt devices yet, or a scan is being forced
        openthings_scan(11);
    }

    // allocate the memory for the deviceList, 100 chars per device + headers
    char *devices = malloc(50 + (g_NumDevices * 100));

    // begin message
    sprintf(devices, "{\"numDevices\":%d, \"devices\":[\n", g_NumDevices);

    for (i = 0; i < g_NumDevices; i++)
    {
        // add device to JSON
        sprintf(deviceStr, "{\"mfrId\":%d,\"productId\":%d,\"deviceId\":%d,\"control\":%d,\"product\":\"%s\",\"joined\":%d}",
                g_OTdevices[i].mfrId, g_OTdevices[i].productId, g_OTdevices[i].deviceId, g_OTdevices[i].control, g_OTdevices[i].product, g_OTdevices[i].joined);
        strcat(devices, deviceStr);
        if (i + 1 < g_NumDevices)
        {
            // more records to come add a ',' to JSON array
            strcat(devices, ",\n");
        }
    }

    // close message
    strcat(devices, "]}");

#if defined(TRACE)
    TRACE_OUTS("openthings_deviceList(): Returning: ");
    TRACE_OUTS(devices);
    TRACE_NL();
#endif

    return devices;
}

/*
** openthings_scan() - listen for valid openThings messages until iTimeOut passed
**                     used to discover devices when we have not autodiscovered any or a search is forced in GUI
**
** This is blocking on the UI and the radio, so should only be performed when necessary
** Also adds FSK devices that are in learning mode (5 second button press to initiate)
**
*/
void openthings_scan(int iTimeOut)
{
    struct OTrecord OTrecs[OT_MAX_RECS];
    unsigned char mfrId, productId;
    unsigned int iDeviceId;
    int records, i, j;
    // char OTrecord[100];
    struct RADIO_MSG rxMsg;
    bool joined = false;

    // Clear data
    records = 0;
    iDeviceId = 0;

    TRACE_OUTS("openThings_scan(): called\n");

    /*
    ** Stage 1 - fill the Rx Buffer (with locking between calls)
    */

    // do a few calls to switch to initiate monitor mode and populate the RxBuffer
    for (i = 0; i < iTimeOut; i++)
    {
        if ((lock_ener314rt()) == 0)
        {
            records += empty_radio_Rx_buffer(DT_LEARN);
            unlock_ener314rt();
            if (records >= RX_MSGS)
                break;
        }
        // wait for more messages
        if (i + 1 < iTimeOut)
            delaysec(1);
    }

    /*
    ** Stage 2 - peek ALL the messages in RxMsgs buffer; this is non-destructive
    */
    for (i = 0; i < RX_MSGS; i++)
    {
        if (get_RxMsg(i, &rxMsg) > 0)
        {
            // message available
            records = openThings_decode(rxMsg.msg, &mfrId, &productId, &iDeviceId, OTrecs);

            if (records > 0)
            {
                joined = false;

                // scan records for JOIN requests, and reply to add
                for (j = 0; j < records; j++)
                {
                    if (OTrecs[j].paramId == OTP_JOIN)
                    {
                        TRACE_OUTS("openThings_scan(): New device found, sending ACK: deviceId:");
                        TRACE_OUTN(iDeviceId);
                        TRACE_NL();
                        openThings_joinACK(productId, iDeviceId, 20);
                        joined = true;
                    }
                }
                // Add devices to standard deviceList
                openThings_devicePut(iDeviceId, mfrId, productId, joined);
            }
        }
    }
}

/*
** openThings_joinACK()
** ===================
** Send a JOIN ACK message to a FSK OpenThings based Energenie smart device
**
** The OpenThings messages are comprised of 3 parts:
**  Header  - msgLength, manufacturerId, productId, encryptionPIP, and deviceId
**  Records - The body of the message, in this case a single command to switch the state
**  Footer  - CRC
**
** Functions performed include:
**    encoding of the device and join request
**    formatting and encoding the OpenThings FSK radio request
**    sending the radio request via the ENER314-RT RaspberryPi adaptor
**
** NOTE: There is an extremely small chance we could lose an incoming message here, but as we are adding new devices it's not worth bothering
**
*/
int openThings_joinACK(unsigned char iProductId, unsigned int iDeviceId, unsigned char xmits)
{
    int ret = 0;
    unsigned short crc;
    unsigned char radio_msg[OTA_MSGLEN] = {OTA_MSGLEN - 1, ENERGENIE_MFRID, PRODUCTID_MIHO005, OT_DEFAULT_PIP, OT_DEFAULT_DEVICEID, OTC_JOIN_ACK, 0x00, 0x00};

    // printf("openThings_joinACK(): productId=%d, deviceId=%d\n", iProductId, iDeviceId);

    /*
    ** Stage 1: Build the message to send
    */

    // TODO: remove this, and use build_msg instead

    /* Stage 1a: OpenThings HEADER
     */
    radio_msg[OTH_INDEX_PRODUCTID] = iProductId;
    // deviceId
    radio_msg[OTH_INDEX_DEVICEID] = (iDeviceId >> 16) & 0xFF;    // MSB
    radio_msg[OTH_INDEX_DEVICEID + 1] = (iDeviceId >> 8) & 0xFF; // MID
    radio_msg[OTH_INDEX_DEVICEID + 2] = iDeviceId & 0xFF;        // LSB

    /*
    ** Stage 1b: OpenThings RECORDS (Commands) - Not required, as ACK record is always the same
    */

    /*
    ** Stage 1c: OpenThings FOOTER (CRC)
    */
    crc = calculateCRC(&radio_msg[5], (OTA_MSGLEN - 7));
    radio_msg[OTA_MSGLEN - 2] = ((crc >> 8) & 0xFF); // MSB
    radio_msg[OTA_MSGLEN - 1] = (crc & 0xFF);        // LSB

#if defined(TRACE)
    TRACE_OUTS("ACK tx payload (unencrypted):\n");
    for (int i = 0; i < OTA_MSGLEN; i++)
    {
        TRACE_OUTN(radio_msg[i]);
        TRACE_OUTC(',');
    }
    TRACE_NL();
#endif

    // Stage 1d: encrypt body part of message (default PIP is OK here)
    cryptMsg(CRYPT_PID, CRYPT_PIP, &radio_msg[5], (OTA_MSGLEN - 5));

    // mutex access radio adaptor
    if ((ret = lock_ener314rt()) != 0)
    {
        return -1;
    }
    else
    {
        /*
        ** Stage 3: Transmit via radio adaptor, using mutex to block the radio
        */
        // Transmit encoded payload 26ms per payload * xmits
        radio_mod_transmit(RADIO_MODULATION_FSK, radio_msg, OTA_MSGLEN, xmits);

        // release mutex lock
        unlock_ener314rt();
    }

    return ret;
}

/*
** openThings_cache_send()
** ===================
** Send any cached command to an OpenThings based smart device
** This is designed for devices that have a small receive window such as the energenie 'MiHome Heating' eTRV
**
** Store cached command using openThings_cache_cmd() before calling this function
**
** NOTE: This uses the device index, not the deviceId
*/
void openThings_cache_send(unsigned char index)
{
    unsigned char msglen;

    /*
    ** The full command is cached in the g_OTdevices.cache structure
    */

    // first check if we have already have cached command for the device; these take precedence
    if (g_OTdevices[index].cache != NULL && g_OTdevices[index].cache->retries > 0)
    {
        msglen = g_OTdevices[index].cache->radio_msg[0] + 1; // msglen in radio message doesn't include the length byte :)
        if (msglen > 1)
        {
            // we have a cached command, send it
            if ((lock_ener314rt()) == 0)
            {
                radio_mod_transmit(RADIO_MODULATION_FSK, g_OTdevices[index].cache->radio_msg, msglen, 1); // TODO make xmits configurable
#ifdef TRACE
            printf("openThings_cache_send(): sent cached cmd %d:%g for device %d\n",g_OTdevices[index].cache->command,g_OTdevices[index].cache->data,g_OTdevices[index].deviceId);
#endif

                // Check if PreCached and swap over globals (within lock)
                if (g_PreCachedCmds > 0 && !g_OTdevices[index].cache->active)
                {
                    // TODO: added mutex
                    _update_cachedcmd_count(-1, false);
                    g_OTdevices[index].cache->active = true;
                    _update_cachedcmd_count(1, true);
                    TRACE_OUTS("openThings_cache_send(): swapped g_counts\n");
                }
                unlock_ener314rt();
                g_OTdevices[index].cache->retries--;

                // If we have reached 0 retries, decrement cachedCmd count and reset the command too
                if (g_OTdevices[index].cache->retries == 0)
                {
                    _update_cachedcmd_count(-1, g_OTdevices[index].cache->active);
                    g_OTdevices[index].cache->command = 0;
                }

#if defined(TRACE)
                printf("openThings_cache_send(): g_CachedCmds=%d, g_PreCachedCmds=%d, deviceId=%d, retries=%d\n", g_CachedCmds, g_PreCachedCmds, g_OTdevices[index].deviceId, g_OTdevices[index].cache->retries);
#endif
            }
        }
    }
}

/*
** eTRV_update()
** ===================
** Store Rx record data in the eTRV record structure for reporting
**
**  OTdi - Index in g_OTdevices array (for speed)
**  OTrecord - The record received
*/
void eTRV_update(int OTdi, struct OTrecord OTrec, time_t updateTime)
{
    TRACE_OUTS("eTRV_update()\n");

    struct TRV_DEVICE *trvData;

    // check that we have the appropriate structures defined
    if (g_OTdevices[OTdi].trv != NULL && g_OTdevices[OTdi].cache != NULL)
    {
        trvData = g_OTdevices[OTdi].trv; // make a pointer to correct struct in array for speed

        switch (OTrec.paramId)
        {
        case OTP_TEMPERATURE:
            trvData->currentC = OTrec.retFloat;
            break;
        case OTP_VOLTAGE:
            trvData->voltage = OTrec.retFloat;
            trvData->voltageDate = updateTime;

            // Do we need to clear cached cmd retries?
            if (g_OTdevices[OTdi].cache->command == OTCP_REQUEST_VOLTAGE)
            {
                // TODO: add mutex
                g_OTdevices[OTdi].cache->command = 0;
                g_OTdevices[OTdi].cache->retries = 0;
                _update_cachedcmd_count(-1, g_OTdevices[OTdi].cache->active);
            }
            break;
        case OTP_DIAGNOSTICS:
            trvData->diagnostics = OTrec.retInt;
            trvData->diagnosticDate = updateTime;
            trvData->errors = false; // clear errors, will set again below
            trvData->errString[0] = '\0';

            // Do we need to clear cached cmd retries? (Exercise valve cmd returns diags too!)
            if (g_OTdevices[OTdi].cache->command == OTCP_REQUEST_DIAGNOSTICS || g_OTdevices[OTdi].cache->command == OTCP_EXERCISE_VALVE)
            {
                g_OTdevices[OTdi].cache->command = 0;
                g_OTdevices[OTdi].cache->retries = 0;
                _update_cachedcmd_count(-1, g_OTdevices[OTdi].cache->active);
            }

            // Is there any specific diag data we need to store as well?
            if (OTrec.retInt > 0)
            {
                // we have diagnostic flags
                if (OTrec.retInt & 0x0001)
                { // Motor current below expectation
                    trvData->errors = true;
                    strncpy(trvData->errString, "Motor current below expectation.", MAX_ERRSTR);
                }
                if (OTrec.retInt & 0x0002)
                { // Motor current always high
                    trvData->errors = true;
                    strncat(trvData->errString, "Motor current always high.", MAX_ERRSTR);
                }
                if (OTrec.retInt & 0x0004)
                { // Motor taking too long
                    trvData->errors = true;
                    strncat(trvData->errString, "Motor taking too long to open/close.", MAX_ERRSTR);
                }
                if (OTrec.retInt & 0x0008)
                { // Discrepancy between air and pipe sensors
                    strncat(trvData->errString, "Discrepancy between air and pipe sensors.", MAX_ERRSTR);
                }
                if (OTrec.retInt & 0x0010)
                { // Air sensor out of expected range
                    trvData->errors = true;
                    strncat(trvData->errString, "Air sensor out of expected range.", MAX_ERRSTR);
                }
                if (OTrec.retInt & 0x0020)
                { // Pipe sensor out of expected range
                    trvData->errors = true;
                    strncat(trvData->errString, "Pipe sensor out of expected range.", MAX_ERRSTR);
                }
                if (OTrec.retInt & 0x0040)
                { // LOW_POWER_MODE
                    trvData->lowPowerMode = true;
                }
                else
                {
                    trvData->lowPowerMode = false;
                }
                if (OTrec.retInt & 0x0080)
                { // No target temperature has been set by host
                    trvData->targetC = 0;
                }
                if (OTrec.retInt & 0x0100)
                { // Valve may be sticking
                    trvData->valve = ERROR;
                    trvData->errors = true;
                    strncat(trvData->errString, "Valve may be sticking.", MAX_ERRSTR);

                }
                if (OTrec.retInt & 0x0200)
                { // EXERCISE_VALVE success
                    trvData->exerciseValve = true;
                    trvData->valveDate = updateTime;
                }
                if (OTrec.retInt & 0x0400)
                { // EXERCISE_VALVE fail
                    trvData->exerciseValve = false;
                    trvData->valveDate = updateTime;
                    trvData->errors = true;
                    strncat(trvData->errString, "Exercise Valve failed.", MAX_ERRSTR);

                }
                if (OTrec.retInt & 0x0800)
                { // Driver micro has suffered a watchdog reset and needs data refresh
                    trvData->errors = true;
                    strncat(trvData->errString, "Driver micro watchdog reset, data refresh needed", MAX_ERRSTR);
                }
                if (OTrec.retInt & 0x1000)
                { // Driver micro has suffered a noise reset and needs data refresh
                    trvData->errors = true;
                    strncat(trvData->errString, "Driver micro noise reset, data refresh needed", MAX_ERRSTR);
                }
                if (OTrec.retInt & 0x2000)
                { // Battery voltage has fallen below 2p2V and valve has been opened
                    trvData->errors = true;
                    strncat(trvData->errString, "Battery voltage below 2.2V, valve opened", MAX_ERRSTR);
                }
                if (OTrec.retInt & 0x4000)
                { // Request for heat messaging is enabled - not sure what to do here, or even how to set this!
                  // trvData->
                }
                if (OTrec.retInt & 0x8000)
                { // Request for heat  - not sure what to do here
                  // trvData->
                }
            }
            else
            {
                // some flags may need clearing as we have 0
                trvData->lowPowerMode = false;
            }
        }
    }
    else
    {
        TRACE_FAIL("eTRV_update(): ERROR: cache or trv structure undefined\n");
    }
}

/*
** eTRV_get_status()
** ===================
** JSONify stored data for the eTRV record structure for reporting
** data is appended to incoming buf as key value comma separated pairs
**
**  OTdi - Index in g_OTdevices array (for speed)
**  buf  - buf appended with new data
**  buflen - length of buffer to prevent memory errors
*/
void eTRV_get_status(int OTdi, char *buf, unsigned int buflen)
{
    struct TRV_DEVICE *trvData;
    trvData = g_OTdevices[OTdi].trv; // make a pointer to correct struct in array for speed
    char trvStatus[200] = "";
    static const char *VALVE_STR[] = {"open", "closed", "auto", "error", "unknown"};

    // populate cached command (even if retries is 0)
    if (g_OTdevices[OTdi].cache != NULL)
    {
        sprintf(trvStatus, ",\"command\":%d,\"retries\":%d",
                g_OTdevices[OTdi].cache->command,
                g_OTdevices[OTdi].cache->retries);
        strncat(buf, trvStatus, buflen);
    }
    if (g_OTdevices[OTdi].trv != NULL)
    {
        if (trvData->targetC > 0)
        {
            sprintf(trvStatus, ",\"TARGET_TEMP\":%.1f", trvData->targetC);
            strncat(buf, trvStatus, buflen);
        }
        if (trvData->voltage > 0)
        {
            sprintf(trvStatus, ",\"VOLTAGE\":%.2f,\"VOLTAGE_TS\":%d", trvData->voltage, (int)trvData->voltageDate);
            strncat(buf, trvStatus, buflen);
        }
        if (trvData->valve != UNKNOWN)
        {
            sprintf(trvStatus, ",\"VALVE_STATE\":\"%s\"", VALVE_STR[trvData->valve]);
            strncat(buf, trvStatus, buflen);
        }
        if (trvData->valveDate > 0)
        {
            sprintf(trvStatus, ",\"EXERCISE_VALVE\":\"%s\",\"VALVE_TS\":%d",
                    trvData->exerciseValve ? "success" : "fail",
                    (int)trvData->valveDate);
            strncat(buf, trvStatus, buflen);
        }
        if (trvData->diagnosticDate > 0)
        {
            sprintf(trvStatus, ",\"DIAGNOSTICS\":%d,\"DIAGNOSTICS_TS\":%d,\"LOW_POWER_MODE\":%s,\"ERRORS\":%s,\"ERROR_TEXT\":\"%s\"",
                    trvData->diagnostics,
                    (int)trvData->diagnosticDate,
                    trvData->lowPowerMode ? "true" : "false",
                    trvData->errors ? "true" : "false",
                    trvData->errString);
            strncat(buf, trvStatus, buflen);
        }
    }
    else
    {
        TRACE_FAIL("eTRV_get_status(): ERROR: trv structure is undefined\n");
    }
}

// private function that operates under a mutex to update the globals to cached/pre-cached commands
void _update_cachedcmd_count(int delta, bool isCached)
{

#ifdef TRACE
    TRACE_OUTS("_update_cachedcmd_count(");
    TRACE_OUTN(delta);
    TRACE_OUTC(',');
    TRACE_OUTN(isCached);
    TRACE_OUTS(") has set cached=");
#endif

    // lock mutex
    if ((pthread_mutex_lock(&cachedcount_mutex)) == 0)
    {
        // update cached or precached command count
        if (isCached)
        {
            // cached
            if (delta > 0)
            {
                // inc
                g_CachedCmds++;
            }
            else
            {
                // dec
                if (g_CachedCmds > 0)
                    g_CachedCmds--;
            }
        }
        else
        {
            // preCached
            if (delta > 0)
            {
                // inc
                g_PreCachedCmds++;
            }
            else
            {
                // dec
                if (g_PreCachedCmds > 0)
                    g_PreCachedCmds--;
            }
        }

        // unlock mutex
        pthread_mutex_unlock(&cachedcount_mutex);

#ifdef TRACE
        TRACE_OUTN(g_CachedCmds);
        TRACE_OUTS(", pre-cached=");
        TRACE_OUTN(g_PreCachedCmds);
        TRACE_NL();
#endif
    }
    else
    {
        printf("_update_cachedcmd_count(): Failed to obtain lock ret=%d\n", errno);
    }
}

/*
** _openThings_build_record()
** ===================
** Creates a single OpenThings command record for sending to an FSK OpenThings based device.
** The full message (header, crc etc) is not built here
**
** SUPPORTED Commands (x) - e=MIHO013=eTRV, t=MIHO069=thermostat, s=MIHO005=Adapter+
** -Param-----------------------Value--Test-Description
**  OTCP_EXERCISE_VALVE         A3 163 e   Send exercise valve command to TRV
**  OTCP_SET_LOW_POWER_MODE     A4 164 ?   Set TRV 0=Low power mode off, 1=Low power mode on
**  OTCP_SET_VALVE_STATE        A5 165 e   Set TRV valve state (0,1,2)
**  OTCP_REQUEST_DIAGNOSTICS    A6 166 e   Request diagnostic flags
**  OTCP_SET_THERMOSTAT_MODE    AA 170 t?   Set mode of Room Thermostat (0,1,2)
**  OTCP_RELAY_POLARITY         AB 171 t   Set relay polarity (of thermostat) (0,1)
**  OTCP_HUMID_OFFSET           BA 186 t   Humidity calibration (of thermostat)
**  OTCP_TEMP_OFFSET            BD 189 t   Temperature calibration (of thermostat)
**  OTCP_IDENTIFY               BF 191 e   Ask device to perform it's identification routine
**  OTCP_SET_REPORTING_INTERVAL D2 210    Update reporting interval to requested value (dont use on thermostat until issue resolved)
**  OTCP_REQUEST_VOLTAGE        E2 226 e   Request battery voltage
**  OTCP_SWITCH_STATE           F3 243    Set status of switched device
**  OTCP_TARGET_TEMP            F4 244 t   Send new target temperature
**  OTCP_HYSTERESIS             FE 254 t   aka Temp Margin, the difference between the current temperature and target temperature before the (thermostat) triggers
**  UNKNOWN                     else  ---- All other commands - returns failure
**
** Parameters:
**  iCommand - the OpenThings command
**  fData    - float data value for the command
**  sRecord  - Pre-allocated buffer used to store the encoded record
**  (return) - Length of built record (-ve if failure)
*/
int _openThings_build_record(unsigned char iCommand, float fData, unsigned char *sRecord)
{
    int reclen = 0;
    unsigned char iType = 0x00;
    int iData;

#define NO_DATA 0xFF

    /* Assign data type based upon OpenThings parameter */
    switch (iCommand)
    {
    case OTCP_SET_LOW_POWER_MODE:
    case OTCP_SWITCH_STATE:
    case OTCP_SET_VALVE_STATE:
    case OTCP_SET_THERMOSTAT_MODE:
    case OTCP_RELAY_POLARITY: // Thermostat RELAY_POLARITY
        iType = 0x01;
        break;

    case OTCP_TARGET_TEMP:
    case OTCP_TEMP_OFFSET: // Thermostat TEMPERATURE_OFFSET
        iType = 0x92; // bit weird, but it works
        break;

    case OTCP_REQUEST_DIAGNOSTICS:
    case OTCP_EXERCISE_VALVE:
    case OTCP_REQUEST_VOLTAGE:
    case OTCP_IDENTIFY:
        iType = NO_DATA;
        break;
    
    case OTCP_SET_REPORTING_INTERVAL:
//        iType = 0x04;       // Thermostat - but doesn't work properly
        iType = 0x02;      // eTRV
        break;

    case OTCP_HYSTERESIS: // Thermostat HYSTERESIS
        iType = 0x11;
        break;

    case OTCP_HUMID_OFFSET: // Thermostat HUMIDITY_OFFSET
        iType = 0x81;
        break;

    case 0xCB: // Thermostat SET_TARGET_TEMPERATURE (energenie email Jan 24 - doesn't seem to work)
        iType = 0x12;
        break;

    default:
        iType = 0;
        // unknown command, exit
#if defined(TRACE)
        printf("_openThings_build_record(): UNKNOWN command=%d\n", iCommand);
#endif
    }

#ifdef TRACE
    printf("_openThings_build_record(): cmd=0x%02x(%d), data=%g, type=%d\n", iCommand, iCommand, fData, iType);
#endif

    /*
    ** Build OpenThings RECORD (Commands)
    */
    if (iType > 0)
    {
        // command & data type
        sRecord[0] = iCommand;
        sRecord[1] = iType;

        // populate data value(s) dependent upon datatype set by command above, using passed in float fData
        switch (iType)
        {
        case NO_DATA:
            reclen = 2;
            sRecord[1] = 0;     // No type for no data
            // no data required
            break;
        case 0x01:  // Unsigned int
        case 0x81:  // Signed int
            reclen = 3;
            iData = (int)fData;
            sRecord[2] = iData & 0xFF;
            break;
        case 0x02:
            reclen = 4;
            iData = (int)fData;
            sRecord[2] = (iData >> 8) & 0xFF;
            sRecord[3] = iData & 0xFF;
            break;            
        case 0x04:      // Int 4 bytes  - tests reclen=4=reply,reclen=5=ignore,reclen=6=ignore
            reclen = 6;
            long long int lData = (long long int)fData;
            sRecord[5] = lData & 0xFF;
            sRecord[4] = (lData >> 8) & 0xFF;
            sRecord[3] = (lData >> 16) & 0xFF;
            sRecord[2] = (lData >> 24) & 0xFF;
            break;            
        case 0x11:  // e.g. Hysteresis
            reclen = 3;
            iData = (int)fData * 16;
            sRecord[2] = iData & 0xFF;
            break;    
        case 0x12:
            reclen = 4;
            fData = fData * (float)256.0;
            iData = (int)fData;
            sRecord[2] = (iData >> 8) & 0xFF;
            sRecord[3] = iData & 0xFF;
            break;
        case 0x92:
            reclen = 4;
            fData = fData * (float)256.0;  // float value multiplied by 256 and sent as 2 bytes; so that fractional part becomes the 2nd byte
            iData = (int)fData;
            sRecord[2] = (iData >> 8) & 0xFF;
            sRecord[3] = iData & 0xFF;
            break;
        }

#if defined(TRACE)
        printf("_openThings_build_record(): Built record (unencrypted) len=%d: ", reclen);
        for (int i = 0; i < reclen; i++)
        {
            TRACE_OUTN(sRecord[i]);
            TRACE_OUTC(',');
        }
        TRACE_NL();
#endif
    }

    return reclen;
}