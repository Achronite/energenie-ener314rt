// This is the parent node program for testing forked processes with 2-way comms so as not to block event loop
//
// To execute just use 'node parent.js'
//
// @Achronite - December 2019
//
const { fork } = require('child_process');

const forked = fork("child.js");

forked.on("message", msg => {
    console.log("parent: Message from child", msg);
});

forked.on('close', (code, signal) => {
    console.log(`parent: child process has terminated due to receipt of signal ${signal}`);
    // clear interval timer (causes program to exit as nothing left to do!)
    clearInterval(intervalId);
});

// every now and again ask the child to send a message
var intervalId = setInterval(() => {
    forked.send({ cmd: "send" });
}, 10000);

// ask trv to report diags
var id2 = setInterval(() => {
    //forked.send({ cmd: "close" });
    //forked.send({ cmd: "monitor", enabled: false });
    forked.send({cmd: "cacheCmd",
        otCommand: 166,
        data: 0,
        deviceId: 3989 });
}, 450000);

// start the monitor loop immediately
forked.send({ cmd: "monitor", enabled: true });