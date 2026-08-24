const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');
const { spawn } = require('child_process');
const { findProjectRoot, findToolBinary: resolveToolBinary } = require('./tooling');

let client;
let outputChannel;
const activeCompilations = new Map();

function findToolBinary(context, baseName, settingName) {
    const config = vscode.workspace.getConfiguration('enginescript');
    return resolveToolBinary({
        extensionPath: context.extensionPath,
        baseName,
        configuredPath: config.get(settingName),
        workspaceFolders: vscode.workspace.workspaceFolders || []
    });
}

function projectRootForDocument(document) {
    return findProjectRoot(document.uri.fsPath);
}

function projectRootForWorkspace() {
    const active = vscode.window.activeTextEditor?.document;
    const activeRoot = active ? projectRootForDocument(active) : undefined;
    if (activeRoot) return activeRoot;
    for (const folder of vscode.workspace.workspaceFolders || []) {
        const root = findProjectRoot(folder.uri.fsPath);
        if (root) return root;
    }
    return undefined;
}

function reportMissingTool(baseName, settingName) {
    vscode.window.showErrorMessage(
        `EngineScript ${baseName} was not found. Build Engine2D, install a matching VSIX, or set enginescript.${settingName}.`
    );
}

function compileProject(context, root, showProgress) {
    const active = activeCompilations.get(root);
    if (active) {
        return active;
    }

    const compilerPath = findToolBinary(context, 'e2sc', 'compilerPath');
    if (!compilerPath) {
        reportMissingTool('compiler', 'compilerPath');
        return Promise.resolve(-1);
    }
    outputChannel.appendLine(`Compiling ${root}`);

    const run = () => new Promise((resolve) => {
        const processHandle = spawn(compilerPath, [root], { cwd: root });
        let settled = false;
        const finish = (code) => {
            if (settled) {
                return;
            }
            settled = true;
            resolve(code);
        };

        processHandle.stdout.on('data', (data) => outputChannel.append(data.toString()));
        processHandle.stderr.on('data', (data) => outputChannel.append(data.toString()));
        processHandle.on('error', (error) => {
            outputChannel.appendLine(`Unable to start compiler: ${error.message}`);
            finish(-1);
        });
        processHandle.on('close', (code) => finish(code ?? -1));
    });

    const compilation = showProgress
        ? vscode.window.withProgress(
            { location: vscode.ProgressLocation.Notification, title: 'Compiling EngineScript project' },
            run
        )
        : run();

    const tracked = compilation.finally(() => activeCompilations.delete(root));
    activeCompilations.set(root, tracked);
    return tracked;
}

function activate(context) {
    outputChannel = vscode.window.createOutputChannel('EngineScript');
    context.subscriptions.push(outputChannel);

    const serverPath = findToolBinary(context, 'e2s-lsp', 'serverPath');
    const projectRoot = projectRootForWorkspace();

    if (!projectRoot) {
        vscode.window.showWarningMessage('Open an Engine2D project containing project.engine2d to enable EngineScript.');
        return;
    }
    if (!serverPath) {
        reportMissingTool('language server', 'serverPath');
        return;
    }

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
            const root = projectRootForWorkspace();
            if (!root) {
                vscode.window.showWarningMessage('Open an Engine2D project before compiling EngineScript.');
                return;
            }

            outputChannel.clear();
            outputChannel.show(true);
            const exitCode = await compileProject(context, root, true);

            if (exitCode === 0) {
                vscode.window.showInformationMessage('EngineScript project compiled successfully.');
            } else {
                vscode.window.showErrorMessage('EngineScript compilation failed. See the EngineScript output.');
            }
        })
    );

    context.subscriptions.push(
        vscode.workspace.onDidSaveTextDocument(async (document) => {
            if (document.languageId !== 'enginescript' && !document.uri.fsPath.endsWith('.e2s')) {
                return;
            }

            const config = vscode.workspace.getConfiguration('enginescript', document.uri);
            if (!config.get('autoCompileOnSave', true)) {
                return;
            }

            const root = projectRootForDocument(document);
            if (!root) {
                return;
            }

            const exitCode = await compileProject(context, root, false);
            if (exitCode === 0) {
                vscode.window.setStatusBarMessage('EngineScript compiled successfully.', 2500);
            } else {
                outputChannel.show(true);
                vscode.window.showErrorMessage('EngineScript compilation failed after save. See the EngineScript output.');
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
