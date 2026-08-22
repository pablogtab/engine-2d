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

## Read API and entity identity

- `get_scene_summary` returns bounded scene and selection counts.
- `find_scene_entities` searches the frozen scene by case-insensitive name and/or
  registered component key. Results contain the stable numeric `entity_id` needed
  by authoring calls.
- `get_entity_details` returns hierarchy, transform, component keys, and serialized
  component data for one existing entity.
- `get_selection_context` provides compact context for the editor selection.
- `find_project_assets` returns bounded project asset matches and their stable
  `asset_handle`, type, and readiness status.
- `get_asset_details` returns dependencies, dependents, and import settings for one
  stable handle without giving the model arbitrary filesystem access.

Existing scene entities must always be addressed by `entity_id`. New entities
receive request-local aliases such as `@enemy`; later actions in that same proposal
may use the alias only after its create action. Aliases never cross conversation
turns and never become persisted asset or entity identity. On every new turn,
Codex resolves targets again from the latest frozen scene state. This makes the
workflow iterative without allowing stale aliases to point at the wrong entity.
Persistent Codex threads record the editor tool-contract version. When that
contract changes, the editor starts a compatible replacement thread and injects
the bounded local conversation history because App Server only accepts dynamic
tool declarations on `thread/start`, not `thread/resume`.

## Authoring action set

- `propose_create_entity`: creates the required Tag, Transform, and Hierarchy
  components plus allowlisted registered defaults.
- `propose_set_transform`: updates bounded, finite Transform fields.
- `propose_rename_entity`: changes a Tag name.
- `propose_add_component` and `propose_remove_component`: use the central component
  registry and its snapshot, finalization, validation, and removal hooks.
- `propose_configure_component`: applies an RFC 7396-style JSON merge patch to the
  current serialized component data. Codex inspects the component first, sends only
  changed fields, and the central registry deserializes, finalizes, and validates
  the result before it becomes an undoable command.
- `propose_reparent_entity`: uses the normal hierarchy command and its cycle checks.
- `propose_delete_entity`: uses the normal reversible subtree deletion command.
- `propose_duplicate_entities` and `propose_delete_entities`: operate on up to 20
  resolved entity subtrees in one queued action.
- `propose_instantiate_prefab`: resolves a ready prefab handle through the asset
  database, creates an alias for its root, and uses the normal prefab command.
- `propose_unpack_prefab`: reversibly removes prefab links from an instance.
- `propose_import_generated_texture`: accepts only a PNG emitted by Codex ImageGen
  during the current request, copies it to a new path under `assets/`, registers its
  stable handle, and assigns that one imported asset to up to 20 Sprite Renderer or
  UI Image targets in the same transaction. Repeated imports of the same generated
  source or destination are rejected while the proposal is still being built.

## Codex texture generation

Codex-subscription conversations can attach the installed `imagegen` skill when
the prompt requests a texture/image or when **Generate PNG with Codex** is enabled
from the composer `+` menu. This uses App Server's built-in image-generation
capability and does not require `OPENAI_API_KEY` or API credits.

App Server writes its generated artifact outside the read-only workspace. The
editor accepts only a bounded PNG reported by the current turn, copies it to a
temporary `Library/EditorAi/TexturePreviews` entry, and shows that preview with the
pending proposal. Confirming imports the bytes through `CreateBinaryAssetCommand`;
dismissing, cancelling, or rejecting removes the cached preview. The original
Codex artifact is not deleted. Once imported, the generated asset participates in
the normal AssetDatabase and can be found by stable handle on later conversation
turns. During the same persistent Codex thread, follow-ups such as "make it darker"
reattach ImageGen and edit the previously generated image into a new versioned
asset instead of losing the visual context.

Assistant tool results mean "queued for validation", never "already applied".
The conversation records a separate Editor message after Apply, automatic apply,
rejection, or dismissal so its visible history reflects the real transaction
result. A failed grouped import removes both its temporary file and any new
unreferenced MissingSource identity once the failed command is abandoned.

## Safety and rollback

- A proposal is capped at 25 authoring actions; bulk entity calls are capped at 20
  unique IDs.
- Tool schemas reject unknown fields, unsupported components, invalid aliases,
  malformed or oversized component patches, non-finite values, zero scale,
  missing entities, and locked entities. Forward or stale aliases fail during the
  tool call so Codex can correct the request before the proposal is completed.
- Required components and `PrefabLinkComponent` cannot be changed through generic
  component tools.
- A failed child command rolls back all earlier child commands. Failed execution is
  not inserted into history.
- Undo reverses children in reverse order. Redo is also transactional and restores
  the applied state only if every child succeeds.
- One successful proposal creates one authoring revision and one Undo entry.
- Asset fields in component patches are checked against the frozen asset index for
  handle existence, ready status, and expected type before execution.
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
provider startup, App Server item lifecycle, time to first reasoning summary,
text or tool, gaps between provider events, per-item and per-tool duration,
queued action count, request duration, and token usage. Prompt/message content,
reasoning-summary text, API keys, and credentials are never logged.

Codex turns request the App Server's public `auto` reasoning summary. The panel
streams this summary into a collapsible Activity block together with timestamped
item and editor-tool lifecycle entries. Raw `item/reasoning/textDelta` content is
intentionally ignored: the UI exposes the supported summary, not private model
reasoning. Activity is transient request diagnostics and is not persisted in the
conversation store.

Every successful application also emits a structured receipt containing the
request ID, before/after revisions, queued action summaries, and bounded lists of
added, removed, and updated entities and assets. For a newly generated texture,
the compact receipt includes its path and stable handle. The panel can display and
copy this JSON, and its summary is supplied to the next turn so iterative requests
know what the editor actually applied.

Interactive authoring defaults to low reasoning effort, keeps only the most recent
bounded conversation history, limits tool rounds and invalid calls, and asks the
model to perform direct editor actions without inspecting the workspace. The model
preset can be switched between Luna (fastest), Terra (balanced), Sol (highest
capability), or a custom model ID.

## Extension rule

Future script, project-setting, scene, and asset-mutation tools must produce normal
editor commands and join the same proposal transaction. An MCP adapter may expose
this registry to external clients, but it must not implement a second mutation
path.
