const path = require('path');
const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

const fs = require('fs');

let client;

function findServerBinary() {
    const config = vscode.workspace.getConfiguration('enginescript');
    const customPath = config.get('serverPath');
    if (customPath && fs.existsSync(customPath)) {
        return customPath;
    }

    // Check workspace build directory
    if (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length > 0) {
        const root = vscode.workspace.workspaceFolders[0].uri.fsPath;
        const candidateBuild = path.join(root, 'build', 'e2s-lsp');
        if (fs.existsSync(candidateBuild)) {
            return candidateBuild;
        }
    }

    return 'e2s-lsp';
}

function activate(context) {
    const serverPath = findServerBinary();

    const serverOptions = {
        run: { command: serverPath, transport: TransportKind.stdio },
        debug: { command: serverPath, transport: TransportKind.stdio }
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'enginescript' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.e2s')
        }
    };

    client = new LanguageClient(
        'enginescriptLanguageServer',
        'EngineScript Language Server',
        serverOptions,
        clientOptions
    );

    client.start();

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
