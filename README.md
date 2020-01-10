# energenie-ener314rt
A node.js module to control the Energenie line of products via the ENER314-RT add-on board for the Raspberry Pi.

https://energenie4u.co.uk/


[![Maintenance](https://img.shields.io/badge/Maintained%3F-yes-brightgreen.svg)](https://github.com/Achronite/energenie-ener314/graphs/commit-activity)
[![Downloads](https://img.shields.io/npm/dm/energenie-ener314rt.svg)](https://www.npmjs.com/package/energenie-ener314rt)
[![HitCount](http://hits.dwyl.io/achronite/energenie-ener314rt.svg)](http://hits.dwyl.io/achronite/energenie-ener314rt)
[![Dependencies Status](https://img.shields.io/david/Achronite/energenie-ener314rt.svg)](https://david-dm.org/Achronite/energenie-ener314rt)
[![Release](https://img.shields.io/github/release-pre/achronite/energenie-ener314rt.svg)](https://github.com/Achronite/energenie-ener314rt/releases)
[![NPM](https://nodei.co/npm/energenie-ener314rt.png)](https://nodei.co/npm/energenie-ener314rt/)


## Purpose

You can use this node.js module to control and monitor the Energenie MiHome radio based smart devices such as adapters, sockets, lights, thermostats and relays 
on a Raspberry Pi with an **ENER314-RT** board installed (see below for full device list).  This is *instead* of operating the devices using a MiHome Gateway, so this module does not require an internet connection.

This node module also has an accompanying node-red implementation by the same author **node-red-contrib-energenie-ener314rt**, this is much easier to use than this node module directly!

The number of individual devices this module can control is over 4 million, so it should be suitable for most installations!

>NOTE: This module does not currently support the older boards (ENER314/Pi-Mote), the Energenie Wifi sockets or the MiHome Gateway.


## Node exposed functions
These functions are exposed by this module:

|node function|description|Input params|Return|N-API 'C' function
|---|---|---|---|---|
|initEner314rt|Initialise radio adaptor|lock||nf_init_ener314rt
|openThingsSwitch|Switch an FSK device|productId, deviceId, switchState, xmits||nf_openThings_switch
|openThingsDeviceList|List discovered devices|scan|json|nf_openThings_deviceList
|openThingsReceive|Get single message|timeout|json|nf_openThings_receive
|openThingsReceiveThread|Start Receive Thread|timeout, callback|via cb|tf_openThings_receive_thread
|openThingsCacheCmd*|Cache an eTRV Command|deviceId, command, data||nf_openThings_cache_cmd
|stopMonitoring*|Stop Receive Thread|||nf_stop_openThings_receive_thread
|ookSwitch|Switch an OOK device|zone, switchNum, switchState, xmits||nf_ook_switch
|sendRadioMsg|Send raw payload|modulation, xmits, buffer||nf_send_radio_msg
|closeEner314rt|Stop using radio adaptor|||nf_close_ener314rt

\* requires ``openThingsReceiveThread`` function to be active


## Getting Started

1) Plug in your ENER314-RT-VER01 board from Energenie onto the 26 pin or 40 pin connector of your Raspberry Pi.

2) Include this module in your node.js code using ```require('energenie-ener314rt');```

TODO - Full instructions and example node.js code


## Supported Devices

These nodes are designed for energenie RF radio devices in the OOK & FSK (OpenThings) ranges.

I've tested the nodes with all devices that I currently own.  Here is a table showing what each node *should* support, and a tag showing if it has been tested (please let me know of any succesful tests, and I'll update the table):

| Device | Description | Control Node (Blue)|Monitor Node (Pink)|Control+Monitor Node (Purple)|Tested OK|
|---|---|:---:|:---:|:---:|:---:|
||**Node Protocol / Type**|*OOK*|*FSK*|*FSK*
|ENER002|Green Button Adapter|x|||x
|ENER010|MiHome 4 gang Multiplug|x
|MIHO002|MiHome Adapter (Blue)|x
|MIHO004|MiHome Energy Monitor (Pink)||x
|MIHO005|MiHome Adapter Plus (Purple)| | x | x|x
|MIHO006|MiHome House Monitor|| x
|MIHO007|MiHome Socket (White)|x|||x
|MIHO008|MiHome Light Switch (White)| x
|MIHO013|MiHome Radiator Valve| | x | use eTRV node |x
|MIHO014|Single Pole Relay (inline)| x
|MIHO015|MiHome Relay|x
|MIHO021|MiHome Socket (Nickel)|x|||White
|MIHO022|MiHome Socket (Chrome)|x|||White
|MIHO023|MiHome Socket (Steel)|x|||White
|MIHO024|MiHome Light Switch (Nickel)| x
|MIHO025|MiHome Light Switch (Chrome)| x
|MIHO026|MiHome Light Switch (Steel)| x
|MIHO032|MiHome Motion sensor| | x
|MIHO033|MiHome Open Sensor| | x
|MIHO069|MiHome Heating Thermostat | | x | ?
|MIHO089|MiHome Click - Smart Button||x


## 'Control Only' OOK Zone Rules
* Each Energenie **'Control'** or OOK based device can be assigned to a specifc zone (or house code) and a switch number.
* Each zone is encoded as a 20-bit address (1-1048575 decimal).
* Each zone can contain up to 6 switches (1-6) - NOTE: officially energenie state this is only 4 devices (1-4)
* All devices within the **same** zone can be switched **at the same time** using a switch number of '0'.
* A default zone '0' can be used to use Energenie's default zone (0x6C6C6).

## Processing Monitor Messages

The received messages are passed back to node.js using the callback registered during the ``openThingsReceiveThread``.  These messages conform to the OpenThings parameter standard.
All OpenThings parameters received from the device are decoded and returned using the callback in a json format.

For example the 'Adapter Plus' returns the following parameters:
```
{
    "timestamp": <numeric 'epoch based' timestamp, of when message was read>
    "REAL_POWER": <power in Watts being consumed>
    "REACTIVE_POWER": <Power in volt-ampere reactive (VAR)>
    "VOLTAGE": <Power in Volts>            
    "FREQUENCY": <Radio Frequency in Hz>
    "SWITCH_STATE": <Device State, 0 = off, 1 = on
}
```
Other devices will return other parameters which you can use. I have provided parameter name and type mapping for the known values for received messages.

A full parameter list can be found in C/src/achronite/openThings.c if required.

## MiHome Radiator Valve (eTRV) Support

v0.3+ now supports the MiHome Thermostatic Radiator valve (eTRV).
> WARNING: Due to the way the eTRV works there may be a delay from when a command is sent to it being processed by the device. See **eTRV Command Caching** below

### eTRV Commands
The MiHome Thermostatic Radiator valve (eTRV) can accept commands to perform operations, provide diagnostics or perform self tests.  The documented commands are provided in the table below.

Single commands should be sent using the ``openThingsCacheCmd`` function, using the command as the # numeric values. If there is no .data value, set it to 0.

| Command | # | Description | .data | Response Msg |
|---|:---:|---|---|:---:|
|EXERCISE_VALVE|163|Send exercise valve command, recommended once a week to calibrate eTRV||DIAGNOSTICS|
|SET_LOW_POWER_MODE|164|This is used to enhance battery life by limiting the hunting of the actuator, ie it limits small adjustments to degree of opening, when the room temperature is close to the *TEMP_SET* point. A consequence of the Low Power mode is that it may cause larger errors in controlling room temperature to the set temperature.|0=Off<br>1=On|No*|
|SET_VALVE_STATE|165|Set valve state|0=Open<br>1=Closed<br>2=Auto (default)|No|
|REQUEST_DIAGNOTICS|166|Request diagnostic data from device, if all is OK it will return 0. Otherwise see additional monitored values for status messages||DIAGNOSTICS|
|IDENTIFY|191|Identify the device by making the green light flash on the selected eTRV for 60 seconds||No|
|SET_REPORTING_INTERVAL|210|Update reporting interval to requested value|300-3600 seconds|No|
|REQUEST_VOLTAGE|226|Report current voltage of the batteries||VOLTAGE|
|TEMP_SET|244|Send new target temperature for eTRV.<br>NOTE: The VALVE_STATE must be set to 'Auto' for this to work.|int|No|

> \* Although this will not auto-report, a subsequent call to *REQUEST_DIAGNOTICS* will confirm the *LOW_POWER_MODE* setting

### eTRV Command Caching
The eTRV reports its temperature at the *SET_REPORTING_INTERVAL* (default 5 minutes). The receiver is activated after each *TEMPERATURE* report to listen for commands. The receiver only remains active for 200ms or until a message is received.

To cater for this hardware limitation the ``openThingsReceiveThread`` and ``openThingsCacheCmd`` functions should be used.  Any command sent using the **CacheCmd** function will be held until a TEMPERATURE report is received by the receive thread; at this point the most recent cached message (only 1 is supported) will be sent to the eTRV.  Messages will continue to be resent until they have been succesfully received or until the number of Retries has reached 0.

The reason that a command may be resent multiple times is due to reporting issues. The eTRV devices, unfortunately, do not send acknowledgement for every command type (indicated by a 'No' in the *Response* column in the above table).  This includes the *TEMP_SET* command!  So these commands are always resent for the full number of retries.

> **NOTE:** The performance of node may decrease when an eTRV command is cached due to dynamic polling. The frequency that the radio device is polled by the monitor thread automatically increases by a factor of 200 when a command is cached (it goes from checking every 5 seconds to every 25 milliseconds) this dramatically increases the chance of a message being correctly received sooner.

### eTRV Monitor Messages

To support the MiHome Radiator Valve (MIHO013) aka **'eTRV'** in v0.3 and above, additional code has been added to cache the monitor information for these devices.  An example of the values is shown below, only 'known' values are returned when the eTRV regularly reports the TEMPERATURE.  See table for types and determining when field was last updated:
```
{
    "deviceId":3989,
    "mfrId":4,
    "productId":3,
    "timestamp":1567932119,
    "TEMPERATURE":19.7,
    "EXERCISE_VALVE":"success",
    "VALVE_TS":1567927343,
    "DIAGNOSTICS":512,
    "DIAGNOSTICS_TS":1567927343,
    "LOW_POWER_MODE":false,
    "TARGET_C": 10,
    "VOLTAGE": 3.19,
    "VOLTAGE_TS": 1568036414,
    "ERRORS": true,
    "ERROR_TEXT": ...
}
```

|Parameter|Description|Data Type|Update time|
|---|---|---|---|
|command|number of current command being set to eTRV|int|timestamp|
|retries|The number of remaining retries for 'command' to be sent to eTRV>|int|timestamp|
|DIAGNOSTICS|Numeric diagnostic code, see "ERRORS" for interpretation|int|DIAGNOSTIC_TS|
|DIAGNOSTICS_TS|timestamp of when diagnostics were last received|epoch|DIAGNOSTIC_TS|
|ERRORS|true if an error condition has been detected|boolean|DIAGNOSTIC_TS|
|ERROR_TEXT|error information|string|DIAGNOSTIC_TS|
|EXERCISE_VALVE|The result of the *EXERCISE_VALVE* command| success or fail|DIAGNOSTIC_TS|
|LOW_POWER_MODE|eTRV is in low power mode state>|boolean|DIAGNOSTIC_TS|
|TARGET_C|Target temperature in celcius|int|TEMP_SET command|
|TEMPERATURE|The current temperature in celcius|float|timestamp|
|VALVE_STATE|Current valve mode/state| open, closed, auto, error|VALVE_STATE command *or* DIAGNOSTIC_TS on error|
|VALVE_TS|timestamp of when last *EXERCISE_VALVE* took place|epoch|DIAGNOSTIC_TS|
|VOLTAGE|Current battery voltage|float|VOLTAGE_TS|
|VOLTAGE_TS|Tmestamp of when battery voltage was last received|epoch|VOLTAGE_TS|

## Module Build Instructions
run 'node-gyp rebuild' in this directory to rebuild the node module.

## Change History
| Version | Date | Change details
|---|---|---|
0.3.0|10 Jan 20|First release of this node.js module after being split from node-red-contrib-energenie-ener314rt, and rewritten to use node.js Native API (N-API) for calling C functions.  This version requires node.js v10+ due to the use of N-API threadsafe functions.
0.3.2|10 Jan 20|Initialise the radio adaptor automatically if not already done so on first lock call (remove always init call made in 0.3.1)


## Built With

* [NodeJS](https://nodejs.org/dist/latest-v10.x/docs/api/) - JavaScript runtime built on Chrome's V8 JavaScript engine.
* [N-API](https://nodejs.org/docs/latest-v10.x/api/n-api.html) - Used to wrap C code as a native node.js Addon. N-API is maintained as part of Node.js itself, and produces Application Binary Interface (ABI) stable across all versions of Node.js.

## Authors

* **Achronite** - *Node wrappers, javascript and additional C code for switching, monitoring and locking* - [Achronite](https://github.com/Achronite/energenie-ener314)
* **David Whale** - *Radio C library and python implementation* - [whaleygeek](https://github.com/whaleygeek/pyenergenie)
* **Energenie** - *Original C code base* - [Energenie](https://github.com/Energenie)

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details

## Bugs and Future Work

Future work is detailed on the [github issues page](https://github.com/Achronite/energenie-ener314rt/issues). Please raise any bugs, questions, queries or enhancements you have using this page.

https://github.com/Achronite/energenie-ener314rt/issues



@Achronite - January 2020 - v0.3.2 Beta