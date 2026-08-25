# AI-assisted authoring contract

The AI Assistant uses **Full Access** for Build turns on the Codex-subscription
backend. Codex may inspect, create, modify, and execute files and commands in the
local environment. This is a direct automation path and may change files outside
the editor's Undo/Redo history. Plan turns override the same persistent thread to
a read-only sandbox. Both modes receive a frozen snapshot of the active scene and
selection plus validated editor tools.

Use validated `propose_*` tools for scene changes whenever the result must retain
the editor's atomic validation, serialization, Undo/Redo, and authoring-revision
guarantees. Shell commands and scripts are not editor commands and cannot be
undone by the editor.

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
6. After a successful application, the same conversation automatically starts a
   verification pass against a new frozen snapshot. Codex must inspect the applied
   scene and may either finish or queue one focused correction batch.
7. Applied correction batches repeat the same validation path. The history
   consolidates every batch from the original request into one reversible entry.

The default mode is **Confirm before apply**. **Autonomous safe** skips the human
confirmation click but uses exactly the same validation and command path. In
confirmation mode, every correction batch is reviewed separately. In autonomous
mode, application and verification continue without an extra click.

## Plan and automatic Build transition

The composer exposes the App Server collaboration modes as `Plan` and `Build`.
Plan is not simulated with prompt wording: `turn/start` receives the native
`collaborationMode.mode = plan` preset and a read-only sandbox policy. Read tools,
attached entities/assets, screenshots, project documents, engine capabilities,
and ephemeral viewport inspection remain available. Authoring tools and direct
filesystem writes are rejected.

App Server `turn/plan/updated` notifications populate the live step list. The
completed `plan` item is authoritative, increments the editor-owned revision when
its text changes, and is persisted with the conversation. Follow-up messages stay
in Plan so the user can change scope, assumptions, validation, or ordering without
losing the inspected context. App Server `requestUserInput` questions are rendered
inside the conversation and the turn resumes after every required answer is sent.

`Approve & Build` explicitly freezes the current plan revision. The same transition
also happens automatically when Codex semantically determines that the current
user message explicitly requests implementation and calls the non-mutating
`begin_plan_implementation` intent tool. The policy rejects generic acknowledgements
such as `ok` unless the message also authorizes implementation. After the Plan turn
finishes, the editor changes the next turn to the native default collaboration mode,
restores Full Access, supplies the frozen plan as authoritative scope, and starts
the existing iterative authoring flow. This avoids brittle phrase matching while
keeping the actual mode and permission change under editor control.

## Iterative authoring sessions

An authoring request becomes an iterative session when its first proposal is
applied. The session preserves the original user goal, the target editor session,
the initial authoring revision, cumulative action usage, and the same provider
thread. After each batch, the next pass receives the application receipt and a
fresh scene snapshot. Request-local aliases are intentionally discarded, so the
model must resolve applied entities again by stable ID.

A verification pass must call at least one read-only editor tool. Visual or
spatial tasks are instructed to capture the Scene Viewport. If inspection finds a
concrete mismatch, Codex queues another bounded proposal; if no authoring actions
are queued, the editor marks the session verified only when the scene revision is
still the one that was inspected. Concurrent edits, scene switches, cancellation,
provider failure, or a rejected correction stop the loop without discarding
earlier applied batches.

Each batch remains capped at 25 actions so one atomic transaction stays bounded.
The complete session is governed by a configurable active AI work budget instead
of an arbitrary batch or action count. It defaults to 10 minutes and can be set
from 1 to 30 minutes in Session Settings. Provider execution counts; time waiting
for human confirmation does not. If a correction consumes the remaining budget,
the editor applies it normally and runs one final read-only inspection before
stopping. The panel displays session status, informational batch/action counts,
and used/budgeted AI time. Cancel or Dismiss stops the current pass.

The command history consolidates already-applied batches by their stable starting
revision. It does not execute them again. Undo reverses the complete iterative
session in one step and Redo reapplies it transactionally. Direct Full Access
filesystem or shell effects remain outside this editor transaction.

## Read API and entity identity

- Every request starts with a compact `get_project_overview` result generated
  from `project.engine2d`, the current asset database, active scene, serialized
  gameplay tags, input mappings, and the project's normal Markdown documents.
  It is discovered state rather than a separately maintained AI context file.
- `find_entities_by_gameplay_tag` resolves semantic roles such as `player`,
  `camera.main`, or `collectible.coin` without relying on display-name guesses.
- `list_project_documents`, `read_project_document`, and
  `search_project_documents` expose bounded `AGENTS.md`, `README.md`, and
  `Docs/**/*.md` content. Paths are selected from a scanned allowlist; traversal,
  symlinks, oversized documents, and arbitrary files are rejected.
- `get_engine_capabilities` reports the compiled EngineScript ABI, registered ECS
  components, available editor tools, input mappings, and runtime diagnostics.
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
  stable handle without requiring the model to guess editor asset identity.

A `ready` result from `find_project_assets` is already sufficient for assigning
its handle. Requests call `get_asset_details` only when dependency, dependent, or
import-setting data is needed; readiness is not queried twice.

Existing scene entities must always be addressed by `entity_id`. New entities
receive request-local aliases such as `@enemy`; later actions in that same proposal
may use the alias only after its create action. Aliases never cross conversation
turns and never become persisted asset or entity identity. On every new turn,
Codex resolves targets again from the latest frozen scene state. This makes the
workflow iterative without allowing stale aliases to point at the wrong entity.
Gameplay roles are separate from the author-facing Tag name. A
`GameplayTagsComponent` stores up to 32 validated, lowercase semantic tags and
is serialized, prefab-safe, editable through Undo/Redo, visible to EngineScript,
and automatically included in AI entity snapshots.
Persistent Codex threads record the editor tool-contract version. When that
contract or Full Access policy changes, the editor starts a compatible replacement
thread and injects the bounded local conversation history because App Server binds
dynamic tool declarations on `thread/start`. Collaboration mode and sandbox policy
are overridden per turn, which allows one persistent thread to move safely between
Plan and Build.

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
  during the current request, copies it to a new path under `Assets/`, registers its
  stable handle, and assigns that one imported asset to up to 20 Sprite Renderer or
  UI Image targets in the same transaction. Repeated imports of the same generated
  source or destination are rejected while the proposal is still being built.
- `propose_import_generated_audio`: copies a PCM WAV synthesized by a local command
  or Python script from `Library/EditorAi/AudioPreviews/` to a new path under
  `Assets/Audio/`, then registers the stable audio handle. It is reversible through
  the same proposal transaction, including Undo and Redo.

## Codex texture generation

Codex-subscription conversations can attach the installed `imagegen` skill when
the prompt requests a texture/image or when **Generate PNG with Codex** is enabled
from the composer `+` menu. This uses App Server's built-in image-generation
capability and does not require `OPENAI_API_KEY` or API credits.

App Server may write its generated artifact outside the project workspace. The
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

## Live viewport inspection

Codex can inspect and navigate the live Scene Viewport through a separate,
read-only observation boundary:

- `get_viewport_state` reports the editor camera center, zoom, visible world
  bounds, render size, and render generation.
- `pan_viewport`, `zoom_viewport`, `focus_viewport_entities`,
  `frame_all_viewport`, and `reset_viewport` change only ephemeral editor view
  state. They never dirty the scene or enter Undo/Redo history.
- `capture_viewport` reads the actual SDL render target on the editor thread and
  returns a bounded PNG as visual tool output. Captures are limited to four per
  request, 2048 pixels per dimension, and 12 MiB; temporary capture files are
  removed immediately after validation and encoding.
- `get_scene_diagnostics` runs the normal `SceneValidator` and returns bounded,
  stable diagnostic codes plus related entity and asset identities.

The editor dispatches live tools on its main thread because camera state and SDL
render targets are not worker-thread APIs. Each request is pinned to the active
scene and authoring revision captured at turn start; a scene switch or concurrent
authored edit rejects later live calls instead of inspecting a mixed state.

Viewport navigation obtains the active editor camera from `SceneManager`; scene
data does not own view state. The reusable `ViewportNavigation` helper receives
that camera explicitly and is shared by human input and AI tools.
Authoring actions remain queued until the turn completes, so a viewport capture
observes the currently applied scene, never an uncommitted proposal.

## Screenshot attachments

The composer accepts up to four PNG or JPEG screenshots per message. Paste an
image from the clipboard with `Cmd+V`/`Ctrl+V`, or choose **Attach Scene Viewport
screenshot** from the composer `+` menu. Attached images show a removable preview
and may be sent without additional text. Each image is limited to 12 MiB and
8192 pixels per dimension.

Codex-subscription turns use the App Server's `localImage` input. OpenAI API turns
use a bounded `input_image` data URL. Temporary copies live under
`Library/EditorAi/ScreenshotAttachments` and are removed after the request,
cancel, dismissal, conversation switch, or editor shutdown; they are never
registered as assets and never dirty the scene.

## Safety and rollback

- A proposal is capped at 25 authoring actions. The iterative session uses its
  configurable active-work timer rather than a cumulative action cap. Bulk entity
  calls are capped at 20 unique IDs.
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
- Each successful batch creates an authoring revision. All batches belonging to
  the same iterative request are consolidated into one Undo entry.
- Asset fields in component patches are checked against the frozen asset index for
  handle existence, ready status, and expected type before execution.
- The panel shortcut for undoing the last AI session is available only while that
  session is still the latest edit; otherwise the regular history remains the
  source of truth.

No scene file is saved automatically. Persistence continues to use the editor's
normal save flow and serialization contracts.

## Local command workflows

Full Access makes Codex's native shell, filesystem, and Python capabilities
available alongside the editor APIs. For example, it can synthesize a WAV with the
Python standard library into `Library/EditorAi/AudioPreviews/loop.wav`, then call
`propose_import_generated_audio` with a new `Assets/Audio/loop.wav` destination.
The script itself is external automation and cannot be undone; the asset import is
the editor transaction that can be undone.

## Audit and performance

Every request emits `[AI AUDIT]` records to the editor Console and the current
run's `engine.log`. A request ID connects its start, tool calls, completion,
application or rejection, and optional AI-session undo. Records include backend,
model, reasoning effort, editor mode, scene revision, bounded-context sizes,
provider startup, App Server item lifecycle, time to first reasoning summary,
text or tool, gaps between provider events, per-item and per-tool duration,
live-tool main-thread queue wait, capture size and render generation, queued action
count, request duration, and token usage. Prompt/message content,
reasoning-summary text, API keys, and credentials are never logged.

Codex turns request the App Server's public `auto` reasoning summary. The panel
groups each App Server item into one operation instead of rendering separate
started/completed log lines. The Activity header is open while work is running and
collapsed after completion. Every command, file change, search, image generation,
or editor tool has an optional detail row with its structured arguments, output,
status, duration, and result; individual details are capped at 64 KiB. Response,
plan, receipt, context, status, reasoning-summary, and operation-detail text uses
read-only selectable controls. Raw `item/reasoning/textDelta` content is
intentionally ignored: the UI exposes the supported summary, not private model
reasoning. Activity is transient request diagnostics and is not persisted in the
conversation store. Distinct App Server `agentMessage` items are separated by a
blank line so commentary and later iteration results remain separate paragraphs.

Authoring proposals validate component invariants against the final transaction
state. A new entity may therefore receive a required component and configure it
later in the same proposal without exposing an invalid intermediate state. If the
final state is invalid, the complete transaction rolls back and the receipt names
the failed command, target, component, and registry validation reason instead of
replacing it with a generic execution failure.

The provider also guards the protocol itself. Every Engine2D dynamic tool is
requested directly and one at a time instead of being invoked from programmatic
`exec` code; this includes read-only inspection and authoring tools. If Codex
announces a dynamic editor tool but does not send its execution request within 120 seconds,
or if the App
Server produces no event for 180 seconds, the editor interrupts the turn and
reports a recoverable failure. In-progress Activity rows are closed as failed,
unapplied proposal actions are discarded, and **Retry request** starts again from
a fresh scene snapshot so entity and asset targets are resolved again instead of
reusing stale IDs.

When an automatic verification resumes a persistent thread immediately after an
application, the provider retries the transient `already has an active writer`
condition with bounded backoff. If the writer remains unavailable, the panel says
that application succeeded but verification could not start, preserves every
applied batch, and keeps the raw protocol error only in the request-details
dropdown.

Every successful application also emits a structured receipt containing the
request ID, before/after revisions, queued action summaries, and bounded lists of
added, removed, and updated entities and assets. For a newly generated texture,
the compact receipt includes its path and stable handle. The panel can display and
copy this JSON. Iterative receipts also expose session status, applied batches,
total actions, active work time, and configured work budget; their summary is
supplied to the next pass so Codex knows what the editor actually applied.

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
