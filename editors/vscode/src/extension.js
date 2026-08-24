const path = require('path');
const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');
const fs = require('fs');
const { spawn } = require('child_process');

let client;
let outputChannel;

function platformDirectory() {
    const architecture = process.arch === 'x64' ? 'x64' : process.arch === 'arm64' ? 'arm64' : process.arch;
    return `${process.platform}-${architecture}`;
}

function binaryName(baseName) {
    const isWindows = process.platform === 'win32';
    return isWindows ? `${baseName}.exe` : baseName;
}

function findToolBinary(context, baseName, settingName) {
    const executable = binaryName(baseName);

    const config = vscode.workspace.getConfiguration('enginescript');
    const customPath = config.get(settingName);
    if (customPath && fs.existsSync(customPath)) {
        return customPath;
    }

    // Portable packages keep every native tool below its OS/architecture.
    const embeddedBin = context.asAbsolutePath(path.join('bin', platformDirectory(), executable));
    if (fs.existsSync(embeddedBin)) {
        return embeddedBin;
    }

    // Developer checkout fallback.
    if (vscode.workspace.workspaceFolders) {
        for (const folder of vscode.workspace.workspaceFolders) {
            const root = folder.uri.fsPath;
            const candidates = [
                path.join(root, 'build', executable),
                path.join(root, 'build', 'Debug', executable),
                path.join(root, 'build', 'Release', executable),
                path.join(root, 'bin', executable)
            ];
            for (const c of candidates) {
                if (fs.existsSync(c)) {
                    return c;
                }
            }
        }
    }

    return executable;
}

function activate(context) {
    outputChannel = vscode.window.createOutputChannel('EngineScript');
    context.subscriptions.push(outputChannel);

    const serverPath = findToolBinary(context, 'e2s-lsp', 'serverPath');
    const projectRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;

    const serverOptions = {
        command: serverPath,
        args: projectRoot ? [projectRoot] : []
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

    context.subscriptions.push(
        vscode.commands.registerCommand('enginescript.compileProject', async () => {
            const root = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
            if (!root) {
                vscode.window.showWarningMessage('Open an Engine2D project before compiling EngineScript.');
                return;
            }

            const compilerPath = findToolBinary(context, 'e2sc', 'compilerPath');
            outputChannel.clear();
            outputChannel.show(true);
            outputChannel.appendLine(`Compiling ${root}`);

            const exitCode = await vscode.window.withProgress(
                { location: vscode.ProgressLocation.Notification, title: 'Compiling EngineScript project' },
                () => new Promise((resolve) => {
                    const processHandle = spawn(compilerPath, [root], { cwd: root });
                    processHandle.stdout.on('data', (data) => outputChannel.append(data.toString()));
                    processHandle.stderr.on('data', (data) => outputChannel.append(data.toString()));
                    processHandle.on('error', (error) => {
                        outputChannel.appendLine(`Unable to start compiler: ${error.message}`);
                        resolve(-1);
                    });
                    processHandle.on('close', (code) => resolve(code ?? -1));
                })
            );

            if (exitCode === 0) {
                vscode.window.showInformationMessage('EngineScript project compiled successfully.');
            } else {
                vscode.window.showErrorMessage('EngineScript compilation failed. See the EngineScript output.');
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
