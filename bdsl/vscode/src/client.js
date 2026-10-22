'use strict';
const { execFile } = require('node:child_process');

/** Run the native tool without a shell. Exit 1 is a BDSL error; exit 2 is an operational error. */
function runCompiler(executable, args, source, options = {}) {
    return new Promise((resolve, reject) => {
        if (Buffer.byteLength(source, 'utf8') > 4 * 1024 * 1024) {
            reject(new Error('BDSL document exceeds 4 MiB'));
            return;
        }
        const child = execFile(executable, args, {
            encoding: 'utf8', windowsHide: true, timeout: options.timeout ?? 5000,
            maxBuffer: 8 * 1024 * 1024, signal: options.signal
        }, (error, stdout, stderr) => {
            if (error && !(options.allowDiagnostics && error.code === 1)) {
                reject(new Error(stderr.trim() || error.message, { cause: error }));
                return;
            }
            resolve(stdout);
        });
        // A process which exits before consuming stdin may close the pipe.
        child.stdin.on('error', () => {});
        child.stdin.end(source, 'utf8');
    });
}

function parseDiagnostics(stdout) {
    const result = JSON.parse(stdout);
    if (!Array.isArray(result.diagnostics)) throw new Error(result.error || 'Invalid bdslc diagnostics response');
    for (const item of result.diagnostics) {
        if (!Number.isInteger(item.line) || item.line < 1 || !Number.isInteger(item.column) || item.column < 1 ||
            !Number.isInteger(item.length) || item.length < 1 || typeof item.message !== 'string') {
            throw new Error('Invalid bdslc diagnostic range');
        }
    }
    return result.diagnostics;
}

module.exports = { runCompiler, parseDiagnostics };
