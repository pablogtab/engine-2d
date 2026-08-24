const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');
const { spawn } = require('child_process');
const { findProjectRoot, findToolBinary: resolveToolBinary } = require('./tooling');

let client;
let outputChannel;
let statusBarItem;
const activeCompilations = new Map();

function setStatus(text, tooltip) {
    if (!statusBarItem) return;
    statusBarItem.text = text;
    statusBarItem.tooltip = tooltip;
    statusBarItem.show();
}

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

    statusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
    statusBarItem.command = 'enginescript.compileProject';
    context.subscriptions.push(statusBarItem);
    setStatus('$(sync~spin) EngineScript: starting', 'EngineScript extension is starting');

    context.subscriptions.push(
        vscode.commands.registerCommand('enginescript.compileProject', async () => {
            const root = projectRootForWorkspace();
            if (!root) {
                vscode.window.showWarningMessage('Open an Engine2D project before compiling EngineScript.');
                return;
            }

            outputChannel.clear();
            outputChannel.show(true);
            setStatus('$(sync~spin) EngineScript: compiling', `Compiling ${root}`);
            const exitCode = await compileProject(context, root, true);

            if (exitCode === 0) {
                setStatus('$(check) EngineScript: ready', `Last build succeeded for ${root}`);
                vscode.window.showInformationMessage('EngineScript project compiled successfully.');
            } else {
                setStatus('$(error) EngineScript: build failed', 'Open the EngineScript output for compiler diagnostics');
                vscode.window.showErrorMessage('EngineScript compilation failed. See the EngineScript output.');
            }
        })
    );

    const serverPath = findToolBinary(context, 'e2s-lsp', 'serverPath');
    const projectRoot = projectRootForWorkspace();

    if (!projectRoot) {
        setStatus('$(circle-slash) EngineScript: open project', 'Open a folder containing project.engine2d');
        vscode.window.showWarningMessage('Open an Engine2D project containing project.engine2d to enable EngineScript.');
        return;
    }
    if (!serverPath) {
        setStatus('$(warning) EngineScript: LSP unavailable', 'Configure enginescript.serverPath or install matching tools');
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
            setStatus('$(error) EngineScript: LSP failed', 'Check the EngineScript output and server path');
            console.warn('[EngineScript] Language server start notification:', err);
        });
        setStatus('$(check) EngineScript: ready', `Language server connected to ${projectRoot}`);
    } catch (err) {
        setStatus('$(error) EngineScript: LSP failed', 'Check the EngineScript output and server path');
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
                setStatus('$(check) EngineScript: ready', `Last build succeeded for ${root}`);
                vscode.window.setStatusBarMessage('EngineScript compiled successfully.', 2500);
            } else {
                setStatus('$(error) EngineScript: build failed', 'Open the EngineScript output for compiler diagnostics');
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
