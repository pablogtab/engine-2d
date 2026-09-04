# EngineScript for VS Code

The extension provides syntax highlighting, live diagnostics, completion, hover,
go-to-definition for authored declarations, document symbols, and type-aware
member completion for typed variables, properties, injections, and services.
The language server reads the current EngineScript API catalog, so available
types, global functions, and service methods stay aligned with the compiler.
This includes contextual component methods such as
`RigidBody2D.setIgnoreOneWay`, particle authoring through `Particles` and
`ParticleEmitter2D`, semantic entity lookup and mutation through the
`Entities` gameplay-tag methods, and deterministic numeric conversions.
The current extension also covers interpolated backtick strings, optional
bindings, exhaustive `match`, enums, named/default arguments, and `Deque<T>`.
It recognizes `static` fields and methods, offers snippets and hover help for
`on_init` and the other lifecycle hooks without `override`, and reports invalid
hook signatures or instance access from static methods through the shared
compiler diagnostics.
Numeric type calls such as `Int(value)`, `UInt8(value)`, and `Float64(value)`
provide completion documentation, hover details, and signature help.
The `EngineScript: Compile Project` command builds `.e2s` files. Its status-bar item
shows language-server availability and the result of the latest project build;
click it to compile the active Engine2D project.

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
