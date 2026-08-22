# Engine2D Luau Vendor Record

- Upstream: https://github.com/luau-lang/luau
- Version: 0.735
- Commit: `367f9d83cc29804a6d5938ec85b6116d34d8743b`
- License: MIT (`LICENSE.txt` and `lua_LICENSE.txt`)

Engine2D disables the upstream CLI, web, and test targets. Runtime targets link
the compiler and VM; editor-only strict analysis links `Luau.Analysis` separately.
