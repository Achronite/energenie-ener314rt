//
// This is the child node program for testing forked processes with 2-way comms to not block parent node.js in any way
//
// This is designed to handle all the Rx & Tx requests, it communicates with the parent using process.on & process.send
//
// @Achronite - January 2020
//
"use strict";

var path = require('path');
let monitoring = false;

var events = require('events');
this.events = new events.EventEmitter();

// This uses the local module build, replace if using actual node.js module
var ener314rt = require(path.join(__dirname, '../build/Release/node_energenie_ener314rt'));

// Use this if using publish npm module
// var ener314rt = require('node_energenie_ener314rt'));

// main processing section that does stuff when asked by parent
process.on('message', msg => {
    console.log("child: Message from parent:", msg);
    switch (msg.cmd) {
        case 'init':
        case 'reset':
            // Normally we initialise automatically anyway, this is a forced reset
            console.log("child: reset");
            process.send({ cmd: "initialised" })
            break;
        case 'send':
            //console.log("child: Sending:", msg.payload);
            switch (msg.mode) {
                case 'fsk':
                case 'FSK':
                    break;
                case 'ook':
                case 'OOK':
                    break;
                case 'off':
                    break;
                default:
                // Unknown monitor mode //
            }
            break;
        case 'monitor':
            // start monitoring loop (if not started already)
            console.log("child: Monitoring enabled=", msg.enabled);
            if (!monitoring) {
                monitoring = true;
                //getMonitorMsg();
                startMonitoringThread();
            }
            console.log("child: Monitoring thread started");
            break;
        case 'close':
            console.log("child: closing");
            ener314rt.closeEner314rt();
            process.exit();
        case 'cacheCmd':
            if (msg.data === undefined || msg.data === null){
                msg.data = 0;
            }
            var res = ener314rt.openThingsCacheCmd(msg.deviceId, msg.otCommand, msg.data);
            console.log(`child: otCC cmd=${msg.otCommand} res=${res}`);
            break;
        default:
            console.log("child: Unknown or missing command:", msg.cmd);
    }
});

// monitor mode - non-async version - this works, but does seem to use the main thread loop
// TODO: async version
//
function getMonitorMsg() {
    do {
        var msg = ener314rt.openThingsReceive(true);
        console.log("child: otR complete");
        //scope.log(`received ${msg}`);

        // msg returns -ve int value if nothing received, or a string
        if (typeof (msg) === 'string' || msg instanceof String) {
            // inform the parent that we have a message
            var OTmsg = JSON.parse(msg);
            process.send(OTmsg);
        } else {
            // no message
        }
    } while (monitoring);
};


// monitor thread version in ener314rt uses a callback to return monitor messages directly (collected below), it needs the callback passing in
function startMonitoringThread() {
    ener314rt.openThingsReceiveThread(10000, (msg) => {
        //console.log(`asyncOpenThingsReceive ret=${ret}`);
        console.log(`child: received=${msg}`);
        var OTmsg = JSON.parse(msg);
            process.send(OTmsg);
    });
};

// Initialise
console.log("child: Initialising");
var ret = ener314rt.initEner314rt(false);
console.log(`child: N-API radio_init returned ${ret}`);

// simulate random Rx
/*
setInterval(() => {
    process.send({ payload: counter++ });
}, 4000);
*/