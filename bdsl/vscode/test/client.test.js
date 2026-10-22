'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { runCompiler, parseDiagnostics } = require('../src/client');
const compiler = process.env.BDSLC;
const source = '[skillset]("test")->{[skill]("stick")->{[check]()->{}[declare]()->{<attack>([type] Physical [power] 2)}}}';

test('valid diagnostics protocol', () => {
    assert.deepEqual(parseDiagnostics('{"diagnostics":[]}'), []);
    assert.throws(() => parseDiagnostics('{"error":"missing input"}'), /missing input/);
    assert.throws(() => parseDiagnostics('{"diagnostics":[{"line":0,"column":1,"length":1,"message":"x"}]}'), /range/);
    assert.throws(() => parseDiagnostics('garbage'));
});
test('spawn failure and input limit are reported', async () => {
    await assert.rejects(runCompiler('missing-bdslc-92f54d', [], ''), /ENOENT/);
    await assert.rejects(runCompiler(process.execPath, [], 'a'.repeat(4 * 1024 * 1024 + 1)), /exceeds/);
});
test('timeout and cancellation terminate compiler processes', async () => {
    await assert.rejects(runCompiler(process.execPath, ['-e', 'setTimeout(()=>{}, 10000)'], '', { timeout: 20 }));
    const controller = new AbortController();
    const pending = runCompiler(process.execPath, ['-e', 'setTimeout(()=>{}, 10000)'], '', { signal: controller.signal });
    controller.abort();
    await assert.rejects(pending);
});
test('real C++ compiler supports checks, formatting and generation', { skip: !compiler }, async () => {
    const result = await runCompiler(compiler, ['--check', '--json', '-'], source, { allowDiagnostics: true });
    assert.deepEqual(parseDiagnostics(result), []);
    const invalid = await runCompiler(compiler, ['--check', '--json', '-'], 'bad', { allowDiagnostics: true });
    assert.equal(parseDiagnostics(invalid).length, 1);
    await assert.rejects(runCompiler(compiler, ['--format', '-'], 'bad'), /error/);
    const formatted = await runCompiler(compiler, ['--format', '--indent', '2', '-'], source);
    assert.match(formatted, /\n  \[skill\]/);
    const cpp = await runCompiler(compiler, ['-'], source);
    assert.match(cpp, /write_attack<PHYSICAL>/);
    assert.match(cpp, /END\(test\)/);
});
test('paths containing spaces and shell punctuation are executed as paths', { skip: !compiler }, async () => {
    const folder = fs.mkdtempSync(path.join(os.tmpdir(), 'bdsl test & native '));
    try {
        const copy = path.join(folder, path.basename(compiler));
        fs.copyFileSync(compiler, copy);
        if (process.platform !== 'win32') fs.chmodSync(copy, 0o755);
        const result = await runCompiler(copy, ['--check', '--json', '-'], source, { allowDiagnostics: true });
        assert.deepEqual(parseDiagnostics(result), []);
    } finally { fs.rmSync(folder, { recursive: true }); }
});
