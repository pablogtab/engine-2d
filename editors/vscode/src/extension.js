const path = require('path');
const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');
const fs = require('fs');

let client;

function findServerBinary() {
    const isWindows = process.platform === 'win32';
    const binaryName = isWindows ? 'e2s-lsp.exe' : 'e2s-lsp';

    // 1. User specified custom path in settings
    const config = vscode.workspace.getConfiguration('enginescript');
    const customPath = config.get('serverPath');
    if (customPath && fs.existsSync(customPath)) {
        return customPath;
    }

    // 2. Embedded directly in extension package: editors/vscode/bin/
    const embeddedBin = path.resolve(__dirname, '..', 'bin', binaryName);
    if (fs.existsSync(embeddedBin)) {
        return embeddedBin;
    }

    // 3. Search in open workspace folders
    if (vscode.workspace.workspaceFolders) {
        for (const folder of vscode.workspace.workspaceFolders) {
            const root = folder.uri.fsPath;
            const candidates = [
                path.join(root, 'build', binaryName),
                path.join(root, 'build', 'Debug', binaryName),
                path.join(root, 'build', 'Release', binaryName),
                path.join(root, 'bin', binaryName)
            ];
            for (const c of candidates) {
                if (fs.existsSync(c)) {
                    return c;
                }
            }
        }
    }

    // 4. Default to system PATH
    return binaryName;
}

function activate(context) {
    const serverPath = findServerBinary();

    const serverOptions = {
        command: serverPath,
        args: []
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'enginescript' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.e2s')
        }
    };

    try {
        client = new LanguageClient(
            'enginescriptLanguageServer',
            'EngineScript Language Server',
            serverOptions,
            clientOptions
        );

        client.start().catch((err) => {
            console.warn('[EngineScript] Language server start notification:', err);
        });
    } catch (err) {
        console.warn('[EngineScript] Language client initialization warning:', err);
    }

    context.subscriptions.push(
        vscode.commands.registerCommand('enginescript.restartServer', async () => {
            if (client) {
                await client.stop();
                client.start();
                vscode.window.showInformationMessage('EngineScript Language Server restarted.');
            }
        })
    );
}

function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

module.exports = {
    activate,
    deactivate
};
