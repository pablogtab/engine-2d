# Engine2D project layout

Engine source and game content are separate workspaces. The engine executable
opens exactly one project root selected with `--project` or `ENGINE2D_PROJECT`.

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

`runtime_shader` declares the canonical HLSL input and the asset paths used by
the current cross-platform shader export slice. CMake reads these paths from the
project descriptor; the engine repository does not name a game shader.

## Development

```sh
cmake -B build -S . -DENGINE_PROJECT_DIR=/absolute/path/to/GameProject
cmake --build build
./build/Engine2D --project /absolute/path/to/GameProject
```

`./dev.sh` and `./demo.sh` default to the sibling `../projecto` directory. Set
`ENGINE2D_PROJECT` to use another project. The engine never searches its own
source tree for gameplay assets.

Luau scripts are external source files. Open the game project in a separate
VSCode window and use the Luau Language Server; Engine2D intentionally has no
embedded script editor.

## Export

Runtime staging reads the project selected by `ENGINE_PROJECT_DIR`, compiles its
platform shader variant, and copies only `project.engine2d`, `Assets/`,
`ProjectSettings/`, and the standalone player. `Library/`, editor resources, and
engine source files are excluded.
