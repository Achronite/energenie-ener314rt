/* openThings.h  Achronite, March 2019 - December 2020
 * 
 * Simplified interface for ENER314-RT devices using OpenThings protocol on Raspberry Pi
 */

#ifndef OTSEND_H
#define OTSEND_H

#include <stdlib.h>

#define FSK_MODE 1
#define ENERGENIE_MFRID 0x04

/* OpenThings Product IDs */
#define PRODUCTID_MIHO004 0x01   // Monitor only
#define PRODUCTID_MIHO005 0x02   // Adaptor Plus
#define PRODUCTID_MIHO006 0x05   // House Monitor
#define PRODUCTID_MIHO013 0x03   // eTRV
#define PRODUCTID_MIHO032 0x0C   // FSK motion sensor
#define PRODUCTID_MIHO033 0x0D   // FSK open sensor
#define PRODUCTID_MIHO069 0x12   // Room Thermostat

/* OpenThings Parameter Keys (for read)
** To WRITE/Command any of these add 128 (0x80) to set bit 7
*/
#define OT_PARAM_NAME_LEN 16    // 15 + null termination (Fixes #25)
#define NUM_OT_PARAMS 56
struct OT_PARAM {
  char paramName[OT_PARAM_NAME_LEN];
  char paramId;
};



/* OpenThings Command Parameters - 0x80 added */
#define OTCP_EXERCISE_VALVE  0xA3   /* 163: Send exercise valve command to driver board. 
                                       Read diagnostic flags returned by driver board. 
                                       Send diagnostic flag acknowledgement to driver board. 
                                       Report diagnostic flags to the gateway. 
                                       Flash red LED once every 5 seconds if ‘battery dead’ flag is set.
                                       Length 0
                                    */

#define OTCP_SET_LOW_POWER_MODE 0xA4  /* 164: 0=Low power mode off, 1=Low power mode on
                                         Length 1
                                      */                                       

#define OTCP_SET_VALVE_STATE 0xA5     /* 165: Set valve state:
                                        0 = Set Valve Fully Open
                                        1 = Set Valve Fully Closed
                                        2 = Set Normal Operation
                                       Valve remains either fully open or fully closed until valve state is set to ‘normal operation’.
                                       Red LED flashes continuously while motor is running terminated by three long green LED flashes when valve 
                                       fully open or three long red LED flashes when valve is closed.
                                       Length 1
                                       */
                                      
#define OTCP_REQUEST_DIAGNOSTICS 0xA6  /* 166: Request diagnostic flags.
                                         Flash red LED once every 5 seconds if ‘battery dead’ flag is set.
                                         Length 0
                                      */
#define OTCP_SET_THERMOSTAT_MODE 0xAA // 170: Set thermostat off/auto/permanently on (0,1,2)
#define OTCP_RELAY_POLARITY      0xAB // 171: Thermostat relay priority (0,1) 
#define OTCP_HUMID_OFFSET        0xBA // Thermostat Humidity calibration (-20..20)
#define OTCP_TEMP_OFFSET         0xBD // Thermostat temperature calibration (-20..10)
#define OTCP_IDENTIFY            0xBF // 191
//#define OTCP_SET_TARGET_TEMPERATURE 0xCB // Thermostat (energenie email Jan 24 - this doesn't seem to work)
#define OTCP_SET_REPORTING_INTERVAL 0xD2 /* 210: Update reporting interval to requested value
                                            Length 2
                                         */   
#define OTCP_REQUEST_VOLTAGE 0xE2   /* 226: Request battery voltage. 
                                       Flash red LED 2 times every 5 seconds if voltage is less than 2.4V
                                       Length 0
                                    */
#define OTCP_JOIN            0xEA   // 234
#define OTCP_SWITCH_STATE    0xF3   // 243
#define OTCP_TARGET_TEMP     0xF4   /* 244: Send new target temperature to driver board */
#define OTCP_HYSTERESIS      0xFE   // 254: the difference between the current temperature and target temperature before the (thermostat) triggers

// OpenThings Rx parameters (full list in .c) - these are added here to replace magic numbers in code
#define OTP_ALARM           0x21
#define OTP_THERMOSTAT_MODE 0x2A // Added for MIHO069
#define OTP_DIAGNOSTICS     0x26
#define OTP_DEBUG_OUTPUT    0x2D
#define OTP_IDENTIFY        0x3F
#define OTP_SOURCE_SELECTOR 0x40 // write only
#define OTP_WATER_DETECTOR  0x41
#define OTP_GLASS_BREAKAGE  0x42
#define OTP_CLOSURES        0x43
#define OTP_DOOR_BELL       0x44
#define OTP_ENERGY          0x45
#define OTP_FALL_SENSOR     0x46
#define OTP_GAS_VOLUME      0x47
#define OTP_AIR_PRESSURE    0x48
#define OTP_ILLUMINANCE     0x49
#define OTP_TARGET_TEMP     0x4B
#define OTP_LEVEL           0x4C
#define OTP_RAINFALL        0x4D
#define OTP_APPARENT_POWER  0x50
#define OTP_POWER_FACTOR    0x51
#define OTP_REPORT_PERIOD   0x52
#define OTP_SMOKE_DETECTOR  0x53
#define OTP_TIME_AND_DATE   0x54
#define OTP_VIBRATION       0x56
#define OTP_WATER_VOLUME    0x57
#define OTP_WIND_SPEED      0x58
#define OTP_WAKEUP          0x59
#define OTP_GAS_PRESSURE    0x61
#define OTP_BATTERY_LEVEL   0x62
#define OTP_CO_DETECTOR     0x63
#define OTP_DOOR_SENSOR     0x64
#define OTP_EMERGENCY       0x65
#define OTP_FREQUENCY       0x66
#define OTP_GAS_FLOW_RATE   0x67
#define OTP_REL_HUMIDITY    0x68
#define OTP_CURRENT         0x69
#define OTP_JOIN            0x6A
#define OTP_LIGHT_LEVEL     0x6C
#define OTP_MOTION_DETECTOR 0x6D
#define OTP_OCCUPANCY       0x6F
#define OTP_REAL_POWER      0x70
#define OTP_ROTATION_SPEED  0x72
#define OTP_SWITCH_STATE    0x73
#define OTP_TEMPERATURE     0x74
#define OTP_VOLTAGE         0x76
#define OTP_WATER_FLOW_RATE 0x77
#define OTP_WATER_PRESSURE  0x78
#define OTP_TEST            0xAA

// OpenThings record data types
#define	OT_UINT   0x00
#define	OT_UINT4  0x10    // 4
#define	OT_UINT8  0x20    // 8
#define	OT_UINT12 0x30    // 12
#define	OT_UINT16 0x40    // 16
#define	OT_UINT20 0x50    // 20
#define	OT_UINT24 0x60    // 24
#define	OT_CHAR   0x70
#define	OT_SINT   0x80    // dec=128
#define	OT_SINT8  0x90    // 8
#define	OT_SINT16 0xA0    // 16
#define	OT_SINT24 0xB0    // 24
#define	OT_FLOAT  0xF0    // Not implemented yet

/* OpenThings Commands (as we don't support auto build of these yet) */
// PARAM, TYPEID, VALUE BYTES
#define OTC_SWITCH_ON  0xF3, 0x01, 0x01, 0x00       // Switch ON
#define OTC_SWITCH_OFF 0xF3, 0x01, 0x00, 0x00       // Switch OFF
#define OTC_JOIN_ACK   0x6A, 0x00, 0x00             // Join ACK


// Default keys for OpenThings encryption and decryption
#define CRYPT_PID 242
#define CRYPT_PIP 0x0100
#define OT_DEFAULT_PIP 0x01, 0x00
#define OT_DEFAULT_DEVICEID 0x00, 0x20, 0x66

#define OT_MAX_RECS 0xF

// OT Msg lengths and positions
#define MIN_R1_MSGLEN 13
#define MAX_R1_MSGLEN 15
#define OTS_MSGLEN 14       // Switch command - Length with 1 command 1 byte sent  (3)
#define OTA_MSGLEN 13       // ACK command    - Length with 1 command 0 bytes sent (2)
#define OTH_INDEX_MFRID     1
#define OTH_INDEX_PRODUCTID 2
#define OTH_INDEX_PIP       3
#define OTH_INDEX_DEVICEID  5
#define OT_INDEX_R1_CMD     8
#define OT_INDEX_R1_TYPE    9
#define OT_INDEX_R1_VALUE  10 

// OpenThings record
struct OTrecord {
    bool cmd;
    unsigned char paramId;
    char paramName[OT_PARAM_NAME_LEN];
    unsigned char typeId;
    char  typeIndex;
    int   retInt;                // I'm hoping this deals with signed and unsigned values
    float retFloat;
    char  retChar[15];            // Length max is 15 for a record
};

#define OTR_INT 1
#define OTR_FLOAT 2
#define OTR_CHAR 3

// eTRV specific stuff
enum valveState {OPEN = 0, CLOSED = 1, TEMPC = 2, ERROR = 3, UNKNOWN = 4};
#define MAX_ERRSTR 50
#define TRV_TX_RETRIES 10

// Structure for storing cached commands for devices with Small Rx Window
struct CACHED_CMD {
    unsigned char retries;
    unsigned char command;
    float         data;
    bool          active;           // used to indicate if we know the device is active (ie. we have an Rx msg) used for pre-caching
    unsigned char radio_msg[MAX_R1_MSGLEN];
};

// Structure for storing data for eTRV devices, these need to be treated as a special case for
//  1) Inability to retrieve all information from device
//  2) Collating values to report at various points
struct TRV_DEVICE {
    float         targetC;
    float         currentC;
    float         voltage;
    unsigned int  diagnostics;
    bool          errors;
    bool          lowPowerMode;
    bool          exerciseValve;
    enum valveState valve;
    time_t        diagnosticDate;
    time_t        voltageDate;
    time_t        valveDate;
    char errString[MAX_ERRSTR+1];
};

// Structure for storing data for Thermostat devices
enum thermostatMode {OFF = 0, AUTO = 1, ON = 2, GATEWAY = 3};
struct STAT_DEVICE {
    enum thermostatMode mode;
    time_t telemetryDate;
};
#define THERMOSTAT_TX_RETRIES 2
#define THERMOSTAT_AUTO_TELEMETRY_TIME 300  // every 5 minutes

// DeviceList structure
struct OT_DEVICE {
    unsigned int  deviceId;
    unsigned char mfrId;
    unsigned char productId;
    unsigned char control;
    bool          joined;
    char          product[15];
    struct CACHED_CMD *cache;                   // need to malloc if used
    struct TRV_DEVICE *trv;                     // need to malloc if used
    struct STAT_DEVICE *thermostat;             // need to malloc if used
};

#define MAX_DEVICES 30


struct OT_PRODUCT {
    unsigned char mfrId;
    unsigned char productId;
    unsigned char control;
    char          product[15];
};
#define NUM_OT_PRODUCTS 9


/***** FUNCTION PROTOTYPES *****/
int openThings_switch(unsigned char iProductId, unsigned int iDeviceId, unsigned char bSwitchState, unsigned char xmits);
int openThings_cmd(unsigned char iProductId, unsigned int iDeviceId, unsigned char command, float fData, unsigned char xmits);
char * openThings_deviceList(bool scan);
int openThings_receive(char *OTmsg, unsigned int buflen, unsigned int timeout);
int openThings_joinACK(unsigned char iProductId, unsigned int iDeviceId, unsigned char xmits);
void openthings_scan(int iTimeOut);

int openThings_cache_cmd(unsigned char iProductId, unsigned int iDeviceId, unsigned char command, float fData, unsigned char retries);
void openThings_cache_send(unsigned char index);
//int openThings_build_msg(unsigned char iProductId, unsigned int iDeviceId, unsigned char iCommand, unsigned int iData, unsigned char *radio_msg);
void eTRV_update(int OTdi, struct OTrecord OTrec, time_t updateTime);
void eTRV_get_status(int OTdi, char *buf, unsigned int buflen);

#endif

/***** END OF FILE *****/

