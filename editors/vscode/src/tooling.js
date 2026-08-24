const fs = require('fs');
const os = require('os');
const path = require('path');

function platformDirectory(platform = process.platform, architecture = process.arch) {
    const normalizedArchitecture = architecture === 'x64' ? 'x64' : architecture === 'arm64' ? 'arm64' : architecture;
    return `${platform}-${normalizedArchitecture}`;
}

function binaryName(baseName, platform = process.platform) {
    return platform === 'win32' ? `${baseName}.exe` : baseName;
}

function expandHome(filePath, homeDirectory = os.homedir()) {
    if (filePath === '~') return homeDirectory;
    if (filePath.startsWith('~/') || filePath.startsWith('~\\')) {
        return path.join(homeDirectory, filePath.slice(2));
    }
    return filePath;
}

function isExecutableFile(filePath, platform = process.platform) {
    try {
        const stat = fs.statSync(filePath);
        if (!stat.isFile()) return false;
        if (platform === 'win32') return true;
        return (stat.mode & 0o111) !== 0;
    } catch (_) {
        return false;
    }
}

function findProjectRoot(startPath) {
    if (!startPath) return undefined;
    let current = path.resolve(startPath);
    try {
        if (fs.statSync(current).isFile()) current = path.dirname(current);
    } catch (_) {
        return undefined;
    }
    while (true) {
        if (fs.existsSync(path.join(current, 'project.engine2d'))) return current;
        const parent = path.dirname(current);
        if (parent === current) return undefined;
        current = parent;
    }
}

function findToolBinary({ extensionPath, baseName, configuredPath, workspaceFolders = [], platform = process.platform, architecture = process.arch }) {
    const executable = binaryName(baseName, platform);
    const configured = configuredPath ? expandHome(configuredPath) : '';
    if (configured && isExecutableFile(configured, platform)) return configured;

    const embedded = path.join(extensionPath, 'bin', platformDirectory(platform, architecture), executable);
    if (isExecutableFile(embedded, platform)) return embedded;

    for (const folder of workspaceFolders) {
        const root = typeof folder === 'string' ? folder : folder.uri?.fsPath;
        if (!root) continue;
        for (const candidate of [
            path.join(root, 'build', executable),
            path.join(root, 'build', 'Debug', executable),
            path.join(root, 'build', 'Release', executable)
        ]) {
            if (isExecutableFile(candidate, platform)) return candidate;
        }
    }
    return undefined;
}

module.exports = {
    binaryName,
    findProjectRoot,
    findToolBinary,
    platformDirectory
};
