'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const Module = require('node:module');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');

const noopDisposable = () => ({ dispose() {} });
const events = {};
const entries = new Map();
let formatter, command, configured = '', document, saveDestination;
const writes = [], shown = [], errors = [];
class Range { constructor(...args) { this.args = args; } }
class Diagnostic { constructor(range, message) { this.range = range; this.message = message; } }
const vscode = {
    Range, Diagnostic, DiagnosticSeverity: { Error: 0 }, EndOfLine: { CRLF: 2 },
    TextEdit: { replace: (range, text) => ({ range, newText: text }) },
    Uri: { file: file => ({ scheme: 'file', fsPath: file, toString: () => file }) },
    languages: {
        createDiagnosticCollection: () => ({ set: (uri, ds) => entries.set(uri.toString(), ds), delete: uri => entries.delete(uri.toString()), dispose() {} }),
        registerDocumentFormattingEditProvider: (_, provider) => { formatter = provider; return noopDisposable(); }
    },
    commands: { registerCommand: (_, callback) => { command = callback; return noopDisposable(); } },
    window: {
        get activeTextEditor() { return { document }; },
        showSaveDialog: async () => saveDestination,
        showTextDocument: async doc => shown.push(doc),
        showErrorMessage: async message => errors.push(message)
    },
    workspace: {
        isTrusted: true, textDocuments: [],
        getConfiguration: () => ({ get: (key, fallback) => key === 'compilerPath' ? configured : key === 'checkDelay' ? 5 : fallback }),
        getWorkspaceFolder: () => ({ uri: { fsPath: path.resolve(__dirname, '..', '..', '..') } }),
        fs: { writeFile: async (uri, buffer) => writes.push({ uri, buffer }) },
        openTextDocument: async uri => uri
    }
};
for (const name of ['onDidOpenTextDocument', 'onDidChangeTextDocument', 'onDidSaveTextDocument', 'onDidCloseTextDocument', 'onDidChangeConfiguration', 'onDidGrantWorkspaceTrust']) {
    vscode.workspace[name] = callback => { events[name] = callback; return noopDisposable(); };
}
const originalLoad = Module._load;
Module._load = function (name, ...args) { return name === 'vscode' ? vscode : originalLoad.call(this, name, ...args); };
const { activate, compilerPath } = require('../src/extension');
Module._load = originalLoad;
function makeDocument(text) {
    const doc = {
        text, version: 1, isClosed: false, languageId: 'bdsl', eol: 1,
        uri: vscode.Uri.file(path.resolve(__dirname, 'unsaved.bdsl')),
        getText() { return this.text; },
        get lineCount() { return this.text.split('\n').length; },
        lineAt(index) { return { text: this.text.split('\n')[index] }; },
        positionAt(offset) { const lines = this.text.slice(0, offset).split('\n'); return { line: lines.length - 1, character: lines.at(-1).length }; }
    };
    return doc;
}
const token = { isCancellationRequested: false, onCancellationRequested: noopDisposable };
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(predicate) {
    const deadline = Date.now() + 5000;
    while (!predicate() && Date.now() < deadline) await pause(10);
    assert.ok(predicate(), 'extension callback completed');
}

test('discovers root CMake builds and prefers the bundled tool', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'bdsl-root-cmake-'));
    const context = { extensionPath: path.join(root, 'bdsl', 'vscode') };
    const name = process.platform === 'win32' ? 'bdslc.exe' : 'bdslc';
    configured = '';
    function tool(...parts) {
        const file = path.join(root, ...parts, name);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, 'test executable');
        return file;
    }
    try {
        const firstBuild = tool('out', 'build', 'first', 'bdsl');
        assert.equal(compilerPath(context, {}), firstBuild);
        const rootBuild = tool('out', 'build', 'x64-release', 'bdsl', 'Release');
        fs.utimesSync(firstBuild, new Date(1000), new Date(1000));
        assert.equal(compilerPath(context, {}), rootBuild);
        const standardBuild = tool('build', 'bdsl');
        fs.utimesSync(rootBuild, new Date(2000), new Date(2000));
        assert.equal(compilerPath(context, {}), standardBuild);
        const bundled = tool('bdsl', 'vscode', 'bin');
        fs.utimesSync(bundled, new Date(500), new Date(500));
        assert.equal(compilerPath(context, {}), bundled);
    } finally { fs.rmSync(root, { recursive: true }); }
});

test('extension reports missing compiler and disposes pending checks', async () => {
    configured = path.join(__dirname, 'missing-bdslc');
    const context = { extensionPath: path.resolve(__dirname, '..'), subscriptions: [] };
    activate(context);
    document = makeDocument('bad');
    events.onDidOpenTextDocument(document);
    await until(() => entries.has(document.uri.toString()));
    assert.match(entries.get(document.uri.toString())[0].message, /ENOENT/);
    events.onDidCloseTextDocument(document);
    assert.equal(entries.size, 0);
    events.onDidChangeTextDocument({ document });
    context.subscriptions.forEach(item => item.dispose());
    await pause(15);
    assert.equal(entries.size, 0);
});

test('real compiler: unsaved diagnostics, formatting, stale results, close, trust and compile command', { skip: !process.env.BDSLC }, async () => {
    configured = process.env.BDSLC;
    const context = { extensionPath: path.resolve(__dirname, '..'), subscriptions: [] };
    activate(context);
    const good = '[skillset]("test")->{[skill]("stick")->{[check]()->{}[declare]()->{}}}';
    document = makeDocument('bad');
    events.onDidOpenTextDocument(document);
    await until(() => entries.has(document.uri.toString()));
    assert.equal(entries.get(document.uri.toString()).length, 1);
    document.text = good; document.version++;
    events.onDidChangeTextDocument({ document });
    await until(() => entries.get(document.uri.toString())?.length === 0);
    const edits = await formatter.provideDocumentFormattingEdits(document, { tabSize: 2, insertSpaces: true }, token);
    assert.equal(edits.length, 1);
    assert.match(edits[0].newText, /\n  \[skill\]/);
    document.eol = 2;
    const crlf = await formatter.provideDocumentFormattingEdits(document, { tabSize: 4, insertSpaces: false }, token);
    assert.match(crlf[0].newText, /\r\n\t\[skill\]/);
    const stale = formatter.provideDocumentFormattingEdits(document, { tabSize: 4, insertSpaces: true }, token);
    document.version++;
    assert.deepEqual(await stale, []);
    assert.deepEqual(await formatter.provideDocumentFormattingEdits(document, { tabSize: 4, insertSpaces: true }, { isCancellationRequested: true }), []);
    document.text = 'bad'; document.version++;
    events.onDidSaveTextDocument(document);
    document.isClosed = true;
    events.onDidCloseTextDocument(document);
    await pause(60);
    assert.equal(entries.size, 0);
    document.isClosed = false; document.text = good; document.version++;
    vscode.workspace.isTrusted = false;
    assert.deepEqual(await formatter.provideDocumentFormattingEdits(document, { tabSize: 4, insertSpaces: true }, token), []);
    await command();
    assert.equal(writes.length, 0);
    vscode.workspace.isTrusted = true;
    saveDestination = undefined;
    await command();
    assert.equal(writes.length, 0, 'cancelled save does not write');
    saveDestination = document.uri;
    await command();
    assert.equal(writes.length, 0, 'cannot overwrite source');
    assert.match(errors.at(-1), /different/);
    saveDestination = vscode.Uri.file(path.resolve(__dirname, 'output.cpp'));
    await command();
    assert.equal(writes.length, 1);
    assert.match(writes[0].buffer.toString(), /END\(test\)/);
    assert.equal(shown.length, 1);
    context.subscriptions.forEach(item => item.dispose());
});
