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

    // 1. Relative to extension directory in the repository
    const extensionRelativeBuild = path.resolve(__dirname, '../../../build/e2s-lsp');
    if (fs.existsSync(extensionRelativeBuild)) {
        return extensionRelativeBuild;
    }

    // 2. Check open workspace folders
    if (vscode.workspace.workspaceFolders) {
        for (const folder of vscode.workspace.workspaceFolders) {
            const root = folder.uri.fsPath;
            const candidateBuild = path.join(root, 'build', 'e2s-lsp');
            if (fs.existsSync(candidateBuild)) {
                return candidateBuild;
            }
        }
    }

    // 3. Known system fallback locations
    const knownLocations = [
        '/Users/pablogtab/Documents/dev/utils/utils/build/e2s-lsp',
        '/usr/local/bin/e2s-lsp',
        '/opt/homebrew/bin/e2s-lsp'
    ];
    for (const loc of knownLocations) {
        if (fs.existsSync(loc)) {
            return loc;
        }
    }

    return 'e2s-lsp';
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
