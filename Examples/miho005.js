//
// This is a miho005.js it is a quick wrapper to show to create a basic application using the node module energenie-ener314rt
//
// This code will switch a single 'smart plug+' socket on and off
//
// You will need node.js v10+ or greater to execute this
//
// run by:  
//
//  node miho005.js
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
        console.log(`miho005.js: received OTmsg=${msg}`);
    });
};

// every 14 seconds toggle an MIHO005 SmartPlug+
var intervalId = setInterval(() => {
    // Only switch if a device has been found
    let productId = 2;      // MIHO005 Adaptor Plus
    let deviceId = 16222;   // replace this with your deviceId as reported by the discovery results
    switchState = !switchState;
    let xmits = 10;
    var res = ener314rt.openThingsSwitch(productId, deviceId, switchState, xmits);
    console.log(`miho005.js: openThingsSwitch(${productId},${deviceId},${switchState},${xmits}) returned ${res}`);
}, 14000);


// every 30 seconds get the discovered deviceList, this is built-up in memory in the node-module when it finds a device
var intervalId2 = setInterval(() => {
    var devices = ener314rt.openThingsDeviceList(false);
    if (devices.numDevices > 0) {
        devicesFound = true;
        console.log(`miho005.js: deviceList=${devices}`);        
    } else {
        console.log(`miho005.js: No devices found yet, please wait...`);
    }
}, 30000);


// Capture ^C, close properly
process.on('SIGINT', function () {
    console.log("miho005.js: Caught interrupt signal, stopping...");
    ener314rt.stopMonitoring();

    // Allow time for monitor thread to complete after timeout and close properly, do this as a cb to not block main event loop
    setTimeout(function () {
        console.log("miho005.js: finalizing close");
        ener314rt.closeEner314rt();
        process.exit();
    }, 10000);
});

// Main processing here
// 1st, Initialise radio adaptor
console.log("miho005.js: Initialising board");
var ret = ener314rt.initEner314rt(false);
if (ret != 0) {
    console.log(`miho005.js: ERROR: Cannot initialise ENER314-RT board, status=${ret}`);
    console.log("miho005.js: Quitting");
    process.exit();
} else {
    // board initialised, start monitoring thread
    console.log("miho005.js: Starting monitoring...");
    startMonitoringThread();
    console.log("miho005.js: Searching for devices...");
}