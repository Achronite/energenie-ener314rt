// energenie-ener314rt module: index.js entry point
const addon = require('./build/Release/ener314rt.node');

// map to functions provided in N-API .node module; allowing editors to autocomplete :)
module.exports.initEner314rt           = addon.initEner314rt;           // Initialise radio adaptor
module.exports.openThingsSwitch        = addon.openThingsSwitch;        // Switch an FSK device
module.exports.openThingsDeviceList    = addon.openThingsDeviceList;    // List discovered devices
module.exports.openThingsReceive       = addon.openThingsReceive;       // Get single message
module.exports.openThingsReceiveThread = addon.openThingsReceiveThread; // Start Receive Thread
module.exports.openThingsCmd           = addon.openThingsCmd;           // Send a Command immediately to FSK device
module.exports.openThingsCacheCmd      = addon.openThingsCacheCmd;      // Cache an eTRV Command
module.exports.stopMonitoring          = addon.stopMonitoring;          // Stop Receive Thread
module.exports.ookSwitch               = addon.ookSwitch;               // Switch an OOK device (zone, switchNum, switchState, xmits)
module.exports.sendRadioMsg            = addon.sendRadioMsg;            // Send raw payload(modulation, xmits, buffer)
module.exports.closeEner314rt          = addon.closeEner314rt;          // Stop using the radio adaptor