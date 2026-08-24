# Project Boundaries

This repository (`/Users/pablogtab/Documents/dev/utils/utils`) contains the
Engine2D engine only. Keep engine code, tooling, and documentation here free
of game-specific gameplay and assets.

`/Users/pablogtab/Documents/dev/utils/projecto` is a separate external game
project. It owns its own `project.engine2d`, `Assets/`, `ProjectSettings/`,
and `Library/` directories. Open and modify it explicitly when work targets
the game; do not treat it as implicit engine content.

Before reporting an asset import, scene placement, or other editor change in
the external project as complete or visible, verify its applied on-disk and
scene/editor state. A generated file or queued editor proposal alone is not
an applied game-project change.
