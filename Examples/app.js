//
// This is a app.js it is a quick wrapper to show to create a basic application using the node module energenie-ener314rt
//
// You will need node.js v10+ or greater to execute this
//
// run by:  
//
//  node app.js
//
// @Achronite - January 2020
//
"use strict";

var switchState = false;

// If you would rather uses the local module build, replace the next line
var ener314rt = require('energenie-ener314rt');
//var path = require('path');
//var ener314rt = require(path.join(__dirname, 'build/Release/ener314rt'));

// the monitoring thread uses a callback to return monitor messages directly (collected below), it needs the callback passing in
function startMonitoringThread() {
    ener314rt.openThingsReceiveThread(10000, (msg) => {
        //callback - update this to do what you want when a monitor message is received from an energenie device
        console.log(`app.js-cb: received OTmsg=${msg}`);
    });
};

// every 14 seconds toggle an OOK switch/teach message
var intervalId = setInterval(() => {
    let zone = 1;
    let switchNum = 1;
    switchState = !switchState;
    let xmits = 20;
    console.log(`app.js: switching ${zone}:${switchNum}:${switchState}`)
    var ret = ener314rt.ookSwitch(zone, switchNum, switchState, xmits);
}, 14000);


// every 30 seconds get the discovered deviceList, this is built-up in memory in the node-module when it finds a device
var intervalId2 = setInterval(() => {
    var devices = ener314rt.openThingsDeviceList(false);
    console.log(`app.js: deviceList=${devices}`);
}, 30000);


// Capture ^C, close properly
process.on('SIGINT', function () {
    console.log("app.js: Caught interrupt signal, stopping...");
    ener314rt.stopMonitoring();

    // Allow time for monitor thread to complete after timeout and close properly, do this as a cb to not block main event loop
    setTimeout(function () {
        console.log("app.js: finalizing close");
        ener314rt.closeEner314rt();
        process.exit();
    }, 10000);
});

// Main processing here
// 1st, Initialise radio adaptor
console.log("app.js: Initialising board");
var ret = ener314rt.initEner314rt(false);
if (ret != 0) {
    console.log(`app.js: ERROR: Cannot initialise ENER314-RT board, status=${ret}`);
    console.log("app.js: Quitting");
    process.exit();
} else {
    // board initialised, start monitoring thread
    console.log("app.js: Starting monitoring...");
    startMonitoringThread();
}