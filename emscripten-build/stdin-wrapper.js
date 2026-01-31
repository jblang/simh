// Wrapper to provide working stdin for Emscripten in Node.js
// This creates a custom Module that properly handles Node.js stdin

const fs = require('fs');

// Buffer for stdin
let stdinBuffer = [];
let stdinEOF = false;

// Set up stdin in raw mode if it's a TTY
if (process.stdin.isTTY) {
    process.stdin.setRawMode(true);
}

process.stdin.on('data', (chunk) => {
    for (let i = 0; i < chunk.length; i++) {
        stdinBuffer.push(chunk[i]);
    }
});

process.stdin.on('end', () => {
    stdinEOF = true;
});

// Custom Module configuration
global.Module = {
    preRun: [],
    postRun: [],

    print: function(text) {
        if (arguments.length > 1) {
            text = Array.prototype.slice.call(arguments).join(' ');
        }
        console.log(text);
    },

    printErr: function(text) {
        if (arguments.length > 1) {
            text = Array.prototype.slice.call(arguments).join(' ');
        }
        console.error(text);
    },

    // Custom stdin handler
    stdin: function() {
        if (stdinBuffer.length > 0) {
            return stdinBuffer.shift();
        }
        if (stdinEOF) {
            return null;
        }
        // No data available yet
        return null;
    },

    onExit: function(status) {
        if (process.stdin.isTTY) {
            process.stdin.setRawMode(false);
        }
        process.exit(status);
    },

    onAbort: function(what) {
        if (process.stdin.isTTY) {
            process.stdin.setRawMode(false);
        }
        console.error('ABORT:', what);
        process.exit(1);
    }
};

// Handle Ctrl+C
process.on('SIGINT', () => {
    if (process.stdin.isTTY) {
        process.stdin.setRawMode(false);
    }
    process.exit(130);
});

// Now load the Emscripten-generated JS
require('/Users/jblang/repos/simh/BIN/altair.js');
