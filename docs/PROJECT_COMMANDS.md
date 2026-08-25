# Project command registry

`ProjectSettings/Commands.json` is the only command surface exposed to the AI
assistant. It uses schema version `1` and an explicit allowlist. A command has
`name`, `kind`, `program`, `args`, `working_directory`, `timeout_seconds`, and
optional `artifact_patterns`.

Only `build`, `test`, `format`, `export`, and `log` kinds are accepted. The
runner starts `program` directly with its argument array; it never invokes a
shell. `${projectRoot}`, `${engineRoot}`, and `${buildDir}` are the only
expanded placeholders. Working directories are canonicalized and must stay
under the project or engine roots. Timeouts are limited to 30 minutes and
captured output is limited to 64 KiB.

The assistant tools are `get_build_status` and `run_project_command(name)`.
They report a stable result with ID, name, live status, exit code, duration,
bounded output, diagnostics, cancellation/timeout flags, and discovered
artifacts. Only one command may run at a time. Cancelling the active assistant
request also terminates its child process. Start, cancellation, completion, and
every discovered artifact are written to the audit log and appear in the
assistant Activity stream. These are direct Full Access effects, never
authoring proposals or Undo/Redo entries.

Profiler captures are written to `Library/ProfilerCaptures` and deterministic
stress reports to `Library/StressResults`. These directories are generated
project data, not assets; enable them explicitly in an editor file explorer if
its exclude rules hide `Library`.
