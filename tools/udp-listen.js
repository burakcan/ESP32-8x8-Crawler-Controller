#!/usr/bin/env node
/**
 * UDP Log Listener for 8x8 Crawler
 *
 * Listens for ESP32 log messages broadcast over UDP.
 *
 * Usage:
 *   node tools/udp-listen.js [port]
 *
 * Default port: 5555
 *
 * Note: Connect to the 8x8-Crawler WiFi network first.
 */

const dgram = require('dgram');

const PORT = parseInt(process.argv[2]) || 5555;
const server = dgram.createSocket('udp4');

server.on('error', (err) => {
    console.error(`Server error:\n${err.stack}`);
    server.close();
});

server.on('message', (msg, rinfo) => {
    // Print message without extra newline (ESP logs usually include one)
    const text = msg.toString('utf8');
    process.stdout.write(text);
    if (!text.endsWith('\n')) {
        process.stdout.write('\n');
    }
});

server.on('listening', () => {
    const address = server.address();
    console.log(`=== UDP Log Listener ===`);
    console.log(`Listening on port ${address.port}`);
    console.log(`Connect to 8x8-Crawler WiFi to receive logs`);
    console.log(`Press Ctrl+C to exit`);
    console.log(`========================\n`);
});

server.bind(PORT);
