# Engine2D project layout

Engine source and game content are separate workspaces. Starting the editor
without arguments opens the Project Hub. It can create a project, browse to an
existing project, or reopen a recent project. `--project` and
`ENGINE2D_PROJECT` remain available for scripts and direct launches.

```text
GameProject/
  project.engine2d
  Assets/
    Scenes/
    Prefabs/
    Scripts/
    Textures/
    Audio/
    Shaders/
  ProjectSettings/
    Project.json
    AssetDatabase.json
  Library/                 generated, never versioned
  Saves/                   generated, never versioned
```

`project.engine2d` is the project identity and launch contract:

```json
{
  "version": 1,
  "name": "Platformer",
  "startup_scene": "Assets/Scenes/PlatformerDemo.scene",
  "runtime_shader": {
    "source": "Assets/Shaders/night_vision.frag.hlsl",
    "descriptor": "Assets/Shaders/NightVision.shader",
    "variant_base": "Assets/Shaders/night_vision.frag"
  }
}
```

Paths inside serialized content and the Asset Database are project-relative and
use `Assets/` as their canonical root. Stable handles remain the source of asset
identity. `ProjectSettings/` is versionable project configuration; editor caches,
AI conversations, captures, and autosaves belong to `Library/`.

`runtime_shader` is optional. When present, it declares the canonical HLSL input
and asset paths used by the cross-platform shader export slice. A new empty
project does not need a shader; compilation and staging skip that step until a
runtime shader is declared. CMake reads these paths from the project descriptor;
the engine repository does not name a game shader.

## Project Hub

The Project Hub stores its recent-project registry in the operating system's
Engine2D editor preferences directory, not inside a game. Missing projects are
pruned and paths are normalized and deduplicated. Creating a project is staged
in a temporary sibling directory and renamed only after every required file is
written, so a failed creation does not leave a partial game directory.

Use `File > Open Project...` to return to the Hub. Dirty scene changes use the
same Save, Discard, or Cancel guard as exiting the editor. Opening the selected
project then starts a fresh application instance so renderer, asset, scripting,
audio, undo, and editor session state never leak between projects.

A newly created project contains a valid empty `Main.scene`, display settings,
an empty Asset Database, standard asset folders, `.gitignore`, `Library/`, and
`Saves/`. Git initialization remains an explicit developer choice.

## Development

```sh
cmake -B build -S . -DENGINE_PROJECT_DIR=/absolute/path/to/GameProject
cmake --build build
./build/Engine2D --project /absolute/path/to/GameProject
# Or open the graphical selector:
./build/Engine2D
```

`./dev.sh` and `./demo.sh` default to the sibling `../projecto` directory. Set
`ENGINE2D_PROJECT` to use another project. The engine never searches its own
source tree for gameplay assets.

Luau scripts are external source files. Open the game project in a separate
VSCode window and use the Luau Language Server; Engine2D intentionally has no
embedded script editor.

## Export

Runtime staging reads the project selected by `ENGINE_PROJECT_DIR`, compiles its
declared platform shader variant when present, and copies only `project.engine2d`, `Assets/`,
`ProjectSettings/`, and the standalone player. `Library/`, editor resources, and
engine source files are excluded.
