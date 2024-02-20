# eneregenie-ener314rt Change Log

## [Unreleased]

*

## [0.7.2] 2024-02-20

### Added

* Added (alpha) support for raspberry pi 5 [#33](https://github.com/Achronite/energenie-ener314rt/issues/33)
* Created example app for MIHO005
* Support added for MiHome Thermostat (MIHO069), including periodic auto-messaging to get telemetry [#34](https://github.com/Achronite/energenie-ener314rt/issues/34)
* Data passing to cache_cmd() now supports float values (was integer) [#38](https://github.com/Achronite/energenie-ener314rt/issues/38)
* A different mechanism of reporting processed commands has been implemented for the thermostat to that used for the eTRV. When (and only when) the thermostat procesess a command it outputs it's telemetry data.  This mechanism has been exploited to assume that the command just sent to the device (upon WAKEUP) has been processed succesfully, upon which the current cached command and it's data value are added to the resulting monitor message.
* Support added for MiHome Click (MIHO089)

### Fixed

* Submitting a cached command will now replace the exisiting cached command for the device
* Fixed ookSwitch returning non-zero when records in buffer [#32](https://github.com/Achronite/energenie-ener314rt/issues/32)
* Prevented cached commands being sent twice for Thermostat [#37](https://github.com/Achronite/energenie-ener314rt/issues/37)
* Fixed initial load of parameters to adaptor not working after initialisation, which prevented OOK messages being sent correctly when monitoring disabled [#43](https://github.com/Achronite/energenie-ener314rt/issues/43)
 

### Changed

* Switched to gpiod from unsupported WiringPi for LEDs and RESET
* *BREAKING* Added `productId` [#35](https://github.com/Achronite/energenie-ener314rt/issues/35) and `retries` parameters to openThingsCacheCmd
* Always return ERRORS + ERROR_TEXT if DIAGNOSTICS has been re-run for eTRV



## Older change history (relocated here from README) for previous versions

| Version | Date | Change details
|---|---|---|
0.6.0|19 Jan 23|Fixed multiple command caching issue (#24).<br>Hardware driver support added using spidev (Issue #5), which falls back to software driver if unavailable.<br>Extensive rewrite of all communication with adaptor for hardware and software mode.<br>Fixed buffer overflow issue on Ubuntu (Issue #25).<br>Renamed TARGET_C to TARGET_TEMP for eTRV (Issue #20).<br>Add capability for cached/pre-cached commands to be cleared with command=0 (Issue #27).<br>Updated 'joined' flag to only show new joiners since last restart.|
0.5.0|19 Apr 22|Prevent non-cachable devices using openThings_cache_cmd() (Issue #18). Switched device type of MIHO069 thermostat to cacheable. Add code to stop Tx retries for thermostat by checking returned values against the type of cached command (Issue #19). Increased error prevention for all malloc'ed structures.|
0.4.1|19 Feb 21|Reduced internal efficiency 'sleep' from 5s to 0.5s (for non-eTRV send mode) to reduce risk of losing a message (Issue #14). Fix crash when using over 6 devices (Issue #15). Disabled DEBUG logging in npm package.|
0.4.0|06 Dec 20|Added new function to immediately send commands. Added MIHO069 thermostat params. Added support for unknown commands (this assumes a uint as sent datatype) in build_message. Updated Energenie device names. Readme updates, including success tests for 3 more devices from AdamCMC. WARNING: This version contains DEBUG logging.|
0.3.4|09 Feb 20|Replaced all exits with return codes from radio init functions. Added better error reporting for raw Tx call.|
0.3.3|01 Feb 20|Disabled Rx when only OOK devices present. Allow eTRV commands to be cached before valve is detected. Tested Energenie 4-way gang. Improved error handling when radio will not initialise.|
0.3.2|10 Jan 20|Initialise the radio adaptor automatically if not already done so on first lock call (remove always init call made in 0.3.1)|
0.3.0|10 Jan 20|First release of this node.js module after being split from node-red-contrib-energenie-ener314rt, and rewritten to use node.js Native API (N-API) for calling C functions.  This version requires node.js v10+ due to the use of N-API threadsafe functions.|

