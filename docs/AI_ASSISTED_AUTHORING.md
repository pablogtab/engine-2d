# AI-assisted authoring contract

The AI Assistant is an editor integration, not an alternate engine or filesystem
automation path. Codex may inspect allowlisted project documentation and a frozen
snapshot of the active scene and selection. Its Codex App Server workspace remains
read-only.

## Authoring flow

1. The editor captures the active scene, selection, command history, entity-state
   store, and current authoring revision.
2. Codex calls strict `propose_*` tools. Each accepted call only appends a validated
   action to an in-memory proposal; it does not mutate ECS state.
3. The completed proposal is either shown for confirmation or scheduled for safe
   automatic application, depending on the selected panel mode.
4. Application occurs on the editor thread. The editor rejects the proposal if the
   scene, editor session, play mode, or authoring revision changed since capture.
5. The proposal is converted to one `EditorCommandGroup` and executed through the
   active `EditorCommandHistory`.

The default mode is **Confirm before apply**. **Autonomous safe** skips the human
confirmation click but uses exactly the same validation and command path.

## Initial action set

- `propose_create_entity`: creates the required Tag, Transform, and Hierarchy
  components plus allowlisted registered defaults.
- `propose_set_transform`: updates bounded, finite Transform fields.
- `propose_rename_entity`: changes a Tag name.
- `propose_add_component` and `propose_remove_component`: use the central component
  registry and its snapshot, finalization, validation, and removal hooks.
- `propose_reparent_entity`: uses the normal hierarchy command and its cycle checks.
- `propose_delete_entity`: uses the normal reversible subtree deletion command.

New entities receive request-local aliases such as `@enemy`. Later actions may use
an alias only after the corresponding create action. Aliases never become persisted
asset or entity identity.

## Safety and rollback

- A proposal is capped at 10 authoring actions.
- Tool schemas reject unknown fields, unsupported components, invalid aliases,
  non-finite values, zero scale, missing entities, and locked entities.
- Required components and `PrefabLinkComponent` cannot be changed through generic
  component tools.
- A failed child command rolls back all earlier child commands. Failed execution is
  not inserted into history.
- Undo reverses children in reverse order. Redo is also transactional and restores
  the applied state only if every child succeeds.
- One successful proposal creates one authoring revision and one Undo entry.
- The panel shortcut for undoing the last AI session is available only while that
  session is still the latest edit; otherwise the regular history remains the
  source of truth.

No scene file is saved automatically. Persistence continues to use the editor's
normal save flow and serialization contracts.

## Audit and performance

Every request emits `[AI AUDIT]` records to the editor Console and the current
run's `engine.log`. A request ID connects its start, tool calls, completion,
application or rejection, and optional AI-session undo. Records include backend,
model, reasoning effort, editor mode, scene revision, bounded-context sizes,
provider startup, time to first text/tool, per-tool result and duration, queued
action count, request duration, and token usage. Prompt/message content, API keys,
and credentials are never logged.

Interactive authoring defaults to low reasoning effort, keeps only the most recent
bounded conversation history, limits tool rounds and invalid calls, and asks the
model to perform direct editor actions without inspecting the workspace. The model
preset can be switched between Luna (fastest), Terra (balanced), Sol (highest
capability), or a custom model ID.

## Extension rule

Future script, project-setting, prefab, scene, and asset tools must produce normal
editor commands and join the same proposal transaction. An MCP adapter may expose
this registry to external clients, but it must not implement a second mutation
path.
