# EngineScript for VS Code

The extension provides syntax highlighting, diagnostics, completion, hover,
navigation, and the `EngineScript: Compile Project` command for `.e2s` files.

## Native tools

Portable packages store `e2s-lsp` and `e2sc` in platform-specific folders:

```text
bin/
  darwin-arm64/
  darwin-x64/
  linux-arm64/
  linux-x64/
  win32-arm64/
  win32-x64/
```

Each release job must build both tools for its target and place them in the
matching folder before packaging the VSIX. The extension selects only the
folder matching `process.platform` and `process.arch`; it never attempts to run
a macOS binary on Windows or Linux.

Open the external game-project root, the directory that contains
`project.engine2d`, in VSCode. The extension resolves the nearest such root for
each `.e2s` document; it never compiles an arbitrary workspace folder.

For a local Engine2D checkout, the CMake build stages the current platform's
tools automatically. During extension development it can also discover the
tools in the workspace `build` directory. Custom installations can set
`enginescript.serverPath` and `enginescript.compilerPath`.

Automatic compile on save is off by default. The language server already
provides live diagnostics; enable it only when generated output is needed after
each save.
