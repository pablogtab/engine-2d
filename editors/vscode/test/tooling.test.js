const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const test = require('node:test');
const { binaryName, findProjectRoot, findToolBinary, platformDirectory } = require('../src/tooling');

test('uses platform-specific names and directories', () => {
    assert.strictEqual(binaryName('e2sc', 'win32'), 'e2sc.exe');
    assert.strictEqual(binaryName('e2sc', 'darwin'), 'e2sc');
    assert.strictEqual(platformDirectory('darwin', 'arm64'), 'darwin-arm64');
});

test('finds the nearest explicit Engine2D project root', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'enginescript-vscode-project-'));
    const scriptDirectory = path.join(root, 'Assets', 'Scripts', 'Player');
    fs.mkdirSync(scriptDirectory, { recursive: true });
    fs.writeFileSync(path.join(root, 'project.engine2d'), '{}');
    assert.strictEqual(findProjectRoot(scriptDirectory), root);
    assert.strictEqual(findProjectRoot(path.join(root, 'Assets', 'Scripts', 'Player.e2s')), undefined);
    fs.writeFileSync(path.join(scriptDirectory, 'Player.e2s'), '');
    assert.strictEqual(findProjectRoot(path.join(scriptDirectory, 'Player.e2s')), root);
    fs.rmSync(root, { recursive: true, force: true });
});

test('resolves only executable configured or embedded tools', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'enginescript-vscode-tools-'));
    const extensionPath = path.join(root, 'extension');
    const platform = process.platform;
    const architecture = process.arch;
    const executable = binaryName('e2sc', platform);
    const embedded = path.join(extensionPath, 'bin', platformDirectory(platform, architecture), executable);
    fs.mkdirSync(path.dirname(embedded), { recursive: true });
    fs.writeFileSync(embedded, '');
    if (platform !== 'win32') fs.chmodSync(embedded, 0o755);
    assert.strictEqual(findToolBinary({ extensionPath, baseName: 'e2sc', platform, architecture }), embedded);
    assert.strictEqual(findToolBinary({ extensionPath, baseName: 'missing', platform, architecture }), undefined);
    fs.rmSync(root, { recursive: true, force: true });
});
