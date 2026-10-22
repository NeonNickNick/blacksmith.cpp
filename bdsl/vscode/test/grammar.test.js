'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const read = file => JSON.parse(fs.readFileSync(path.join(root, file), 'utf8'));
const manifest = read('package.json');
const grammar = read(manifest.contributes.grammars[0].path);

function patterns(entries) {
    return entries.flatMap(entry => entry.include ? patterns([grammar.repository[entry.include.slice(1)]]) :
        entry.patterns && !entry.begin ? patterns(entry.patterns) : [entry]);
}
const rules = patterns(grammar.patterns);
function scopeOf(text) {
    for (const rule of rules) {
        if (!rule.match) continue;
        const match = new RegExp(rule.match).exec(text);
        if (match?.index === 0) return rule.name;
    }
}

test('grammar registers BDSL and classifies each token category', () => {
    assert.equal(manifest.contributes.grammars[0].language, 'bdsl');
    assert.equal(grammar.scopeName, 'source.bdsl');
    const cases = [
        ['[skillset]', 'keyword.control.block.bdsl'], ['[skill]', 'keyword.control.block.bdsl'],
        ['[check]', 'keyword.control.block.bdsl'], ['[declare]', 'keyword.control.block.bdsl'],
        ['[name]', 'variable.parameter.bdsl'], ['[power]', 'variable.parameter.bdsl'],
        ['[clock]', 'variable.parameter.bdsl'], ['[isinfinite]', 'variable.parameter.bdsl'],
        ['<attack>', 'keyword.other.action.bdsl'], ['<effect>', 'keyword.other.action.bdsl'],
        ['<repeat>', 'keyword.control.flow.bdsl'], ['<if>', 'keyword.control.flow.bdsl'],
        ['<else>', 'keyword.control.flow.bdsl'], ['<require>', 'keyword.control.require.bdsl'],
        ['__N__', 'variable.language.skill-parameter.bdsl'], ['__index__', 'variable.language.repeat-index.bdsl'],
        ['@bonus@', 'variable.other.mark.bdsl'], ['Iron', 'support.type.resource.bdsl'],
        ['CrystalBall', 'support.type.resource.bdsl'], ['HP', 'support.type.resource.bdsl'],
        ['Magical', 'storage.type.attack.bdsl'], ['CommonArmor', 'storage.type.defense.bdsl'],
        ['EraseResource', 'storage.type.effect.bdsl'], ['DefaultReduction', 'support.function.analyzer.bdsl'],
        ['Enemy', 'constant.other.target.bdsl'], ['MAGIC_ATTACK', 'constant.other.enum.bdsl'],
        ['CANNON', 'constant.other.enum.bdsl'], ['false', 'constant.language.boolean.bdsl'],
        ['count(', 'support.function.bdsl'], ['min (', 'support.function.bdsl'],
        ['0.5', 'constant.numeric.bdsl'], ['.5e+1', 'constant.numeric.bdsl'], ['1.', 'constant.numeric.bdsl'],
        ['->', 'keyword.operator.arrow.bdsl'], ['!=', 'keyword.operator.comparison.bdsl'],
        ['<=', 'keyword.operator.comparison.bdsl'], ['!', 'keyword.operator.logical.bdsl'],
        ['&&', 'keyword.operator.logical.bdsl'], ['^', 'keyword.operator.arithmetic.bdsl'],
        ['{', 'punctuation.section.block.bdsl'], ['(', 'punctuation.section.group.bdsl']
    ];
    for (const [text, scope] of cases) assert.equal(scopeOf(text), scope, text);
    for (const text of ['[skillpackagedefinition]', '__skillparam__', '__n__', '__N__suffix', 'Irony', '[unknown]']) {
        assert.equal(scopeOf(text), undefined, text);
    }
});

test('dedicated dark and light themes include explicit BDSL colors', () => {
    assert.deepEqual(manifest.contributes.themes.map(theme => theme.uiTheme).sort(), ['vs', 'vs-dark']);
    for (const item of manifest.contributes.themes) {
        const theme = read(item.path);
        const scopes = new Map(theme.tokenColors.flatMap(rule => (Array.isArray(rule.scope) ? rule.scope : [rule.scope]).map(scope => [scope, rule.settings])));
        for (const scope of ['keyword.control.block.bdsl', 'keyword.other.action.bdsl', 'variable.parameter.bdsl',
            'variable.language.skill-parameter.bdsl', 'variable.other.mark.bdsl', 'support.type.resource.bdsl',
            'constant.other.enum.bdsl', 'constant.language.boolean.bdsl']) {
            assert.match(scopes.get(scope)?.foreground || '', /^#[0-9a-fA-F]{6}$/, `${item.label}: ${scope}`);
        }
        assert.notEqual(scopes.get('variable.language.skill-parameter.bdsl').foreground, scopes.get('variable.parameter.bdsl').foreground);
    }
});

test('snippets use the current skillset and __N__ syntax', () => {
    const snippets = read(manifest.contributes.snippets[0].path);
    const text = JSON.stringify(snippets);
    assert.ok(text.includes('[skillset]'));
    assert.ok(text.includes('__N__'));
    assert.ok(!text.includes('skillpackagedefinition'));
    assert.ok(!text.includes('__skillparam__'));
});

// Optional real engine check: Electron in node mode can load the engines bundled in VSCode.
// BDSL_VSCODE_APP points at its resources/app directory. Normal unit tests need no npm packages.
let engine;
if (process.env.BDSL_VSCODE_APP) {
    const app = process.env.BDSL_VSCODE_APP;
    engine = {
        textmate: require(path.join(app, 'node_modules.asar', 'vscode-textmate', 'release', 'main.js')),
        oniguruma: require(path.join(app, 'node_modules.asar', 'vscode-oniguruma', 'release', 'main.js')),
        wasm: fs.readFileSync(path.join(app, 'node_modules.asar.unpacked', 'vscode-oniguruma', 'release', 'onig.wasm'))
    };
}

test('real TextMate tokenization isolates strings, comments and magic fields', { skip: !engine }, async () => {
    const { textmate, oniguruma, wasm } = engine;
    await oniguruma.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));
    const registry = new textmate.Registry({
        onigLib: Promise.resolve({ createOnigScanner: sources => new oniguruma.OnigScanner(sources), createOnigString: text => new oniguruma.OnigString(text) }),
        loadGrammar: async scope => scope === 'source.bdsl' ? grammar : null
    });
    try {
        const language = await registry.loadGrammar('source.bdsl');
        function scopesAt(text, needle, state) {
            const result = language.tokenizeLine(text, state);
            const index = text.indexOf(needle);
            assert.ok(index >= 0);
            const token = result.tokens.find(item => item.startIndex <= index && item.endIndex > index);
            return { scopes: token.scopes, state: result.ruleStack };
        }
        const line = '[skillset]("demo") -> { <attack>([type] Magical [power] __N__ + 2) }';
        for (const [text, scope] of [['skillset', 'keyword.control.block.bdsl'], ['demo', 'string.quoted.double.bdsl'],
            ['<attack>', 'keyword.other.action.bdsl'], ['power', 'variable.parameter.bdsl'],
            ['Magical', 'storage.type.attack.bdsl'], ['__N__', 'variable.language.skill-parameter.bdsl'], ['2', 'constant.numeric.bdsl']]) {
            assert.ok(scopesAt(line, text).scopes.includes(scope), text);
        }
        assert.ok(scopesAt('<require> __N__ != 0', '!=').scopes.includes('keyword.operator.comparison.bdsl'));
        const string = scopesAt('"[skillset] __N__ // @mark@"', '__N__').scopes;
        assert.ok(string.includes('string.quoted.double.bdsl'));
        assert.ok(!string.some(scope => scope.startsWith('variable.language')));
        assert.ok(scopesAt('// [skillset] __N__ <attack>', '__N__').scopes.includes('comment.line.double-slash.bdsl'));
        const block = scopesAt('/* __N__', '__N__');
        assert.ok(scopesAt('[skillset] */ <use> __N__ Magic', '[skillset]', block.state).scopes.includes('comment.block.bdsl'));
        assert.ok(scopesAt('[skillset] */ <use> __N__ Magic', '__N__', block.state).scopes.includes('variable.language.skill-parameter.bdsl'));
        assert.ok(scopesAt('"a\\\" __N__"', '__N__').scopes.includes('string.quoted.double.bdsl'));
        const malformed = language.tokenizeLine('"unterminated __N__');
        assert.ok(scopesAt('<require> __N__ > 0', '__N__', malformed.ruleStack).scopes.includes('variable.language.skill-parameter.bdsl'));
    } finally { registry.dispose(); }
});
