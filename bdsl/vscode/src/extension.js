'use strict';
const vscode = require('vscode');
const fs = require('node:fs');
const path = require('node:path');
const { runCompiler, parseDiagnostics } = require('./client');

function compilerPath(context, document) {
    const configured = vscode.workspace.getConfiguration('bdsl', document.uri).get('compilerPath', '');
    if (configured) {
        const folder = vscode.workspace.getWorkspaceFolder(document.uri);
        if (!path.isAbsolute(configured) && !folder) throw new Error('Use an absolute bdsl.compilerPath outside a workspace');
        return path.isAbsolute(configured) ? configured : path.resolve(folder.uri.fsPath, configured);
    }
    const name = process.platform === 'win32' ? 'bdslc.exe' : 'bdslc';
    const bundled = path.join(context.extensionPath, 'bin', name);
    if (fs.existsSync(bundled)) return bundled;
    const root = path.resolve(context.extensionPath, '..', '..');
    const buildRoots = [path.join(root, 'build'), path.join(root, 'out', 'build')];
    const presets = path.join(root, 'out', 'build');
    if (fs.existsSync(presets)) {
        for (const entry of fs.readdirSync(presets, { withFileTypes: true })) {
            if (entry.isDirectory()) buildRoots.push(path.join(presets, entry.name));
        }
    }
    const directories = buildRoots.map(build => path.join(build, 'bdsl'));
    const candidates = directories.flatMap(directory => ['', 'Release', 'Debug', 'RelWithDebInfo', 'MinSizeRel'].map(config => path.join(directory, config, name)));
    const found = candidates.filter(candidate => fs.existsSync(candidate))
        .sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
    if (found.length === 0) throw new Error('bdslc was not found. Build the root CMake bdslc target or set bdsl.compilerPath.');
    return found[0];
}

function activate(context) {
    const diagnostics = vscode.languages.createDiagnosticCollection('bdsl');
    const timers = new Map();
    const running = new Map();
    let disposed = false;
    function stop(document) {
        const key = document.uri.toString();
        clearTimeout(timers.get(key)); timers.delete(key);
        running.get(key)?.abort(); running.delete(key);
    }
    function range(document, item) {
        const line = Math.min(document.lineCount - 1, item.line - 1);
        const column = Math.min(document.lineAt(line).text.length, item.column - 1);
        return new vscode.Range(line, column, line, Math.min(document.lineAt(line).text.length, column + item.length));
    }
    async function validate(document) {
        if (disposed || document.isClosed || document.languageId !== 'bdsl' || !vscode.workspace.isTrusted) return;
        stop(document);
        const controller = new AbortController();
        const key = document.uri.toString(), version = document.version;
        running.set(key, controller);
        try {
            const stdout = await runCompiler(compilerPath(context, document), ['--check', '--json', '-'], document.getText(), { signal: controller.signal, allowDiagnostics: true });
            if (disposed || document.isClosed || document.version !== version || controller.signal.aborted) return;
            diagnostics.set(document.uri, parseDiagnostics(stdout).map(item => {
                const diagnostic = new vscode.Diagnostic(range(document, item), item.message, vscode.DiagnosticSeverity.Error);
                diagnostic.source = 'bdslc'; return diagnostic;
            }));
        } catch (error) {
            if (disposed || document.isClosed || document.version !== version || controller.signal.aborted) return;
            const diagnostic = new vscode.Diagnostic(new vscode.Range(0, 0, 0, 0), `BDSL: ${error.message}`, vscode.DiagnosticSeverity.Error);
            diagnostic.source = 'bdslc'; diagnostics.set(document.uri, [diagnostic]);
        } finally {
            if (running.get(key) === controller) running.delete(key);
        }
    }
    function schedule(document) {
        if (document.languageId !== 'bdsl') return;
        stop(document);
        const delay = vscode.workspace.getConfiguration('bdsl', document.uri).get('checkDelay', 250);
        timers.set(document.uri.toString(), setTimeout(() => void validate(document), delay));
    }
    context.subscriptions.push(diagnostics,
        vscode.workspace.onDidOpenTextDocument(document => void validate(document)),
        vscode.workspace.onDidSaveTextDocument(document => void validate(document)),
        vscode.workspace.onDidChangeTextDocument(event => schedule(event.document)),
        vscode.workspace.onDidCloseTextDocument(document => { stop(document); diagnostics.delete(document.uri); }),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (event.affectsConfiguration('bdsl')) vscode.workspace.textDocuments.forEach(document => void validate(document));
        }),
        vscode.workspace.onDidGrantWorkspaceTrust(() => vscode.workspace.textDocuments.forEach(document => void validate(document))),
        vscode.languages.registerDocumentFormattingEditProvider('bdsl', {
            async provideDocumentFormattingEdits(document, options, token) {
                if (!vscode.workspace.isTrusted || token.isCancellationRequested) return [];
                const controller = new AbortController();
                const subscription = token.onCancellationRequested(() => controller.abort());
                const version = document.version;
                try {
                    const args = ['--format', '--indent', String(Math.max(1, Math.min(16, options.tabSize)))];
                    if (!options.insertSpaces) args.push('--tabs');
                    args.push('-');
                    let text = await runCompiler(compilerPath(context, document), args, document.getText(), { signal: controller.signal });
                    if (disposed || token.isCancellationRequested || document.isClosed || document.version !== version) return [];
                    if (document.eol === vscode.EndOfLine.CRLF) text = text.replace(/\r?\n/g, '\r\n');
                    return [vscode.TextEdit.replace(new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), text)];
                } catch (error) {
                    if (!token.isCancellationRequested) void validate(document);
                    return [];
                } finally { subscription.dispose(); }
            }
        }),
        vscode.commands.registerCommand('bdsl.compile', async () => {
            const document = vscode.window.activeTextEditor?.document;
            if (!document || document.languageId !== 'bdsl' || !vscode.workspace.isTrusted) return;
            try {
                // Compile the unsaved buffer before showing a destination dialog.
                const cpp = await runCompiler(compilerPath(context, document), ['-'], document.getText());
                const defaultUri = document.uri.scheme === 'file' ? vscode.Uri.file(document.uri.fsPath.replace(/\.bdsl$/i, '') + '.cpp') : undefined;
                const destination = await vscode.window.showSaveDialog({ defaultUri, filters: { 'C++ source': ['cpp'] }, title: 'Save generated BDSL C++' });
                if (!destination) return;
                if (destination.toString() === document.uri.toString()) throw new Error('Choose a C++ output path different from the BDSL input');
                await vscode.workspace.fs.writeFile(destination, Buffer.from(cpp, 'utf8'));
                await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(destination));
            } catch (error) { void vscode.window.showErrorMessage(`BDSL: ${error.message}`); }
        }),
        { dispose() {
            disposed = true;
            for (const timer of timers.values()) clearTimeout(timer);
            for (const controller of running.values()) controller.abort();
            timers.clear(); running.clear();
        } }
    );
    vscode.workspace.textDocuments.forEach(document => void validate(document));
}

module.exports = { activate, compilerPath };
