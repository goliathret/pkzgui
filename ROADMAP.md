# Goliath Studio X: engine and level editor roadmap

GSX should grow from a package and asset viewer into an editor that can assemble,
edit, save, and run scenes using Edge of Time content. The primary runtime bridge
is [goliathret/EdgeOfTimeRecomp](https://github.com/goliathret/EdgeOfTimeRecomp):
GSX provides authoring tools, while the recomp runtime executes the game and its
existing rendering, animation, audio, and gameplay paths. Build each feature in
small, testable steps and keep the original game assets usable throughout.

Unchecked items are planned work, not promises of existing support. Items marked
**Research** need format or runtime investigation before we can implement them
reliably. The order below is a starting point and can change as we learn more.

## What already works

- [x] Native menus, file dialogs, and persistent light/dark themes.
- [x] Background package loading, resource browsing, fields, and debug log.
- [x] Texture previews, PNG extraction, replacement, and package saving.
- [x] Static model previews with package textures and temporary retextures.
- [x] Orbit camera, model framing, scale grid, and MSAA with a fallback.
- [x] Material shader container and metadata export.
- [x] Audio decoding, waveform preview, play/pause, seeking, and WAV extraction.
- [x] Package, texture, audio, dialog, and model regression tests.
- [x] Release workflow triggered by tags or manual runs.

The current model renderer uses an approximate lighting shader. Shader export
does not mean that we reproduce the game's shader execution. The viewport shows
one selected model; it does not yet contain a persistent scene or simulation.

## Main workflow: play, pause, edit a section, resume

The intended experience is to play the actual game with a controller, notice a
place that needs work, pause there, add or edit a section, and immediately resume
playing to test it. A section can grow from placed geometry into a reusable group
with collision, entities, audio, effects, and triggers.

- [ ] Launch a configured EdgeOfTimeRecomp installation from GSX.
- [ ] Pass controller input to the game while in Play mode.
- [ ] Pause the scene without closing the game or losing the player's location.
- [ ] Keep rendering and editor navigation working while gameplay is paused.
- [ ] Transfer input focus to the editor, then return it cleanly on resume.
- [ ] Inspect the current level, camera/player location, and nearby objects.
- [ ] Select existing objects or place a new section at the paused location.
- [ ] Author edits in a persistent GSX layer, with undo/redo and dirty tracking.
- [ ] Apply supported changes through runtime commands and confirm their results.
- [ ] Resume from the same location and test the changed area with the controller.
- [ ] Save the section and reapply it after restarting or reloading the level.
- [ ] Define which edits work immediately and which require resource/level reload.

The bridge must distinguish saved authoring changes from transient game state.
Resuming or stopping a test must not silently discard a section authored while
paused. Full game-state snapshots and rewind are separate research items.

The editor should expose a clear **Play -> Pause and Edit -> Resume** flow. The
paused game supplies the location and context; GSX supplies selection, placement,
the inspector, and the edit history. Adding a section is only complete when its
collision and relevant behavior work after resuming, not just when a mesh appears.

## First bridge milestone: control a running game

- [ ] Define a versioned local command/event interface between GSX and the runtime.
- [ ] Connect, report runtime/build identity and capabilities, and disconnect cleanly.
- [ ] Request pause/resume and read back the actual state.
- [ ] Read camera position and observe level/package load events.
- [ ] Prove controller input and focus handoff across Play and Edit modes.
- [ ] Keep game rendering in its own window for this initial proof.
- [ ] Measure command latency and verify that simulation pause is effective.
- [ ] Give requests IDs and explicit queued/applied/error results so the editor
      does not mistake an unprocessed command for a completed edit.
- [ ] Handle runtime exits, timeouts, reconnects, and incompatible bridge versions.

Done means we can launch the game from GSX, play to a location, pause, inspect the
camera/level context, and resume without restarting. Object placement follows
once we have verified the runtime's object lifecycle and transform interfaces.

## First authoring milestone: a scene we can save and reopen

- [ ] Create a scene document independently of the selected package resource.
- [ ] Add two models from BaseGameplay.pkz to the same viewport.
- [ ] Give each object a stable ID, name, visibility, and transform.
- [ ] Select objects in the viewport and scene hierarchy.
- [ ] Edit position, rotation, and scale in the inspector.
- [ ] Add move, rotate, and scale gizmos, with snapping and numeric input.
- [ ] Share mesh and texture data between instances of the same asset.
- [ ] Save scene placement and asset references to a versioned GSX scene file.
- [ ] Reopen that file and restore the objects and their transforms.
- [ ] Undo and redo object creation, deletion, and transform changes.
- [ ] Verify that scene editing leaves the source packages unchanged.

Done means those two objects survive selection changes, can move independently,
and appear in the same places after restarting GSX. Build this alongside the
runtime bridge so GSX has a reliable authoring document to send changes from.

## EdgeOfTimeRecomp integration

Source review baseline: `340144e4c7979a9f46699bef368fe7d7c62fddc9`.
Recheck these interfaces when the runtime revision changes.

| Area | Existing source to investigate | GSX work still needed |
| --- | --- | --- |
| Startup/shutdown | `src/reeot_app.h/.cpp` and `ReeotApp` | Launch configuration, session lifecycle, connection and shutdown handling |
| Rendering | `src/gpu/device.h`, `draw.h`, `render_thread.h` | An adapter for runtime rendering and, later, editor viewport presentation |
| Shaders | `src/gpu/shaders/guest_shaders.h/.cpp` | Observe actual shader hashes, variants, bindings, and constants |
| Asset loading | `src/gpu/hooks/package_load.cpp` | Package/material/geometry events and stable editor-to-runtime resource mapping |
| Pause/camera | `src/goliath/debug/freecam.h/.cpp` | Commandable pause/step/freecam controls and state readback |
| Mod handling | `src/mods/mods_api.h`, `mod_manager.h/.cpp` | Compatible project overrides and explicit apply/reload behavior |

- [ ] Add the editor command endpoint in the runtime; these hooks are not already
      a general remote editor API.
- [ ] Queue mutations at verified game-thread/frame boundaries and synchronize
      resource changes with the renderer.
- [ ] Use runtime handles with lifetime/generation checks, not persisted guest pointers.
- [ ] Record package unload, object destruction, level transition, and reconnect events.
- [ ] Discover and validate object enumeration, spawn/despawn, transform updates,
      collision registration, and component changes.
- [ ] Keep game-specific runtime integration separate from GSX's scene representation.
- [ ] Reuse the actual runtime's material/shader state for faithful game previews.
- [ ] Add embedded game output after the separate-window bridge works, with measured
      latency and a suitable cross-platform presentation path.

The runtime renderer uses Plume with guest resource/state handling, while GSX's
current preview uses OpenGL. Copying shader code alone does not connect those
systems. Prefer a separate runtime process first; decide on shared rendering or
in-process hosting after the control interface is proven.

The reviewed scene-pause implementation controls the game's time scale and has
step/freecam hooks. Verify physics, animation, audio, streaming, and script behavior
under pause before treating it as a safe authoring boundary. It is not a complete
save-state or a guarantee that all game threads stop.

For the feature lists below, first look for the corresponding implementation in
EdgeOfTimeRecomp and expose or extend it through the bridge. Rebuilding physics,
animation, audio, or the full game renderer is not a prerequisite for passthrough
play. Independent GSX implementations are for offline previews, new scene types,
or gaps that the runtime does not cover.

## Engine structure

- [ ] Keep game format decoding in PKZLib and platform services outside it.
- [ ] Separate runtime scene data from ImGui widgets and editor selection.
- [ ] Make rendering consume scene data rather than package selection state.
- [ ] Separate immutable assets from per-object runtime state.
- [ ] Define transforms, parent/child relationships, and component ownership.
- [ ] Adapt the recomp runtime lifecycle and simulation timing to editor controls.
- [ ] For GSX-native scenes, add an update loop and a fixed simulation step where needed.
- [ ] Add Edit, Play, Pause, and single-step modes.
- [ ] Separate simulation changes from saved authoring edits during Play mode.
- [ ] Keep background decoding separate from GPU resource creation.
- [ ] Establish clear resource ownership and cleanup during reload and shutdown.
- [ ] Assess a reusable runtime library after the external runtime bridge works.

Start with the components we need: transform, mesh, camera, light, and audio
source. Add more as real features require them.

## Projects, packages, and asset management

- [ ] Project files describing mounted packages, scenes, settings, and overrides.
- [ ] Mount multiple PKZ/PAK files and resolve dependencies across them.
- [ ] Stable asset references that distinguish package, type, CRC, language,
      quality, and resource variants; names alone must not determine identity.
- [ ] Asset registry, dependency inspection, and missing-reference diagnostics.
- [ ] Search and filtering by name, type, package, and usage.
- [ ] Shared CPU/GPU caches with memory budgets and explicit invalidation.
- [ ] Background loading with progress, cancellation, and useful failures.
- [ ] Asset reload without discarding scene placement or editor state.
- [ ] Per-project overrides and a way to compare original and modified assets.
- [ ] Versioned scene/project serialization and migrations.
- [ ] Import/export reports for unsupported or lossy conversions.

## Level editing

- [ ] Scene hierarchy with parenting, grouping, duplicate, delete, and rename.
- [ ] Local/world transforms and consistent units and coordinate conventions.
- [ ] Object picking, multi-selection, focus selection, and free-fly navigation.
- [ ] Grid, angle, vertex, and surface snapping.
- [ ] Inspector editing through undoable commands.
- [ ] Prefabs with reusable object hierarchies and instance overrides.
- [ ] Visibility, locking, layers, and editor-only helpers.
- [ ] Multiple scenes or sublevels, with explicit load/unload boundaries.
- [ ] **Research:** decode game object placement and hierarchy references.
- [ ] Import a small existing game area with correct transforms and dependencies.
- [ ] **Research:** terrain, level geometry, collision, and streaming metadata.
- [ ] Terrain sculpting, surface painting, and foliage/object placement tools once
      the corresponding scene and asset representations are supported.
- [ ] Export supported changes back to game packages after round-trip validation.

GSX scene saving and Xbox level export are separate milestones. We can build and
use our scene editor before every original game structure is understood.

## Models and geometry

- [ ] Multiple renderable instances with independent transforms.
- [ ] Mesh/submesh selection, material assignments, bounds, and wireframe views.
- [ ] LOD inspection and selection, followed by distance-based LOD switching.
- [ ] Preserve the vertex attributes required by materials and skinning.
- [ ] Validate normals, tangents, UV channels, index formats, and handedness.
- [ ] Geometry diagnostics for unsupported declarations and malformed buffers.
- [ ] Import/export through an intermediate format such as glTF where practical.
- [ ] **Research:** geometry replacement, buffer sizing, and package metadata.
- [ ] **Research:** morph targets, attachments, and game-specific geometry variants.

## Textures

- [ ] Shared texture assets with previews, thumbnails, and usage references.
- [ ] Mip, channel, alpha, color-space, and compression inspection.
- [ ] Texture assignment to individual material slots.
- [ ] Replacement preview, original comparison, reload, and reset.
- [ ] Explicit handling of diffuse/color, normal, mask, and other data textures.
- [ ] Preserve supported dimensions, formats, mip chains, and package layout.
- [ ] Rebuild mipmaps and encode supported replacement formats.
- [ ] Diagnose missing textures and unsupported layouts without hiding the model.

## Materials, shaders, and rendering

- [ ] Material instances with editable parameters and texture bindings.
- [ ] Support required UV channels, sampler settings, and material constants.
- [ ] Explicit opaque, cutout, and blended rendering, with appropriate depth state.
- [ ] Directional, point, and spot lights with editable scene components.
- [ ] Consistent color-space handling, exposure, and output conversion.
- [ ] Normal mapping, shadows, sky/environment lighting, and reflection support.
- [ ] Debug views for normals, UVs, depth, material slots, and lighting.
- [ ] Culling, LODs, instancing, and render statistics as scene sizes grow.
- [ ] Keep viewport resources stable across resize and selection changes.
- [ ] **Research:** map game material techniques to passes and required render state.
- [ ] **Research:** use XenosRecomp output for a known material in GSX previews.
- [ ] Supply the vertex layout, constants, textures, and semantics that shader needs.
- [ ] Compare against a game reference and record what is still approximated.
- [ ] Preview shader replacements with compile diagnostics and reload/reset.
- [ ] **Research:** Xbox-compatible shader replacement and package writing.

Host shader compilation is only part of game-faithful rendering. We also need the
correct draw state, resource bindings, constants, and pass ordering. Start with
one understood material rather than claiming broad compatibility.

## Skeletons and animation

- [ ] **Research:** hierarchy decoding, bone names, parent indices, and bind poses.
- [ ] **Research:** bone indices/weights in geometry and animation resource links.
- [ ] Skeleton display and bind-pose validation on one known character.
- [ ] CPU reference skinning, then GPU skinning checked against it.
- [ ] **Research:** animation tracks, compression, timing, and interpolation.
- [ ] Play, pause, scrub, loop, and change playback speed for a single clip.
- [ ] Animation events and attachment/bone inspection.
- [ ] Clip blending, transitions, and animation state machines.
- [ ] Root motion and displacement handling.
- [ ] Morph animation, additive layers, and inverse kinematics where needed.
- [ ] Animation import/export once the underlying representations are validated.

## Audio

- [ ] Audio source/listener scene components with position and orientation.
- [ ] Spatial playback, distance attenuation, looping, and source limits.
- [ ] Mixer buses, mute/solo, and volume controls.
- [ ] Audio cue inspection and event-driven playback.
- [ ] Playback tied to scene simulation and cinematic timelines.
- [ ] **Research:** game sound-bank, cue, language, and streaming relationships.
- [ ] Later: trim/fade and other small editing tools with non-destructive previews.
- [ ] **Research:** audio replacement encoding and required package metadata.

Use the recomp runtime's existing game audio path during passthrough play. The
current GSX FFmpeg path decodes XMA2 to WAV; it does not provide an Xbox XMA2
encoder. Importing a WAV for GSX playback and writing Xbox-compatible replacement
audio need separate implementation paths.

## Physics, collision, and navigation

- [ ] Collision shapes and debug visualization.
- [ ] Static colliders, raycasts, and overlap queries.
- [ ] Rigid bodies, collision layers, triggers, and interaction events.
- [ ] Character movement and a basic controller.
- [ ] Physics materials, joints, and ragdolls when character interactions need them.
- [ ] Scene queries used by picking, placement, gameplay, and audio.
- [ ] **Research:** original collision formats and coordinate/scale conventions.
- [ ] **Research:** navigation data and links used by the original game.
- [ ] Navigation visualization, path queries, and agent movement.

## Game logic and interaction

- [ ] Named entities and components with serializable properties and references.
- [ ] Input actions for keyboard, mouse, and gamepad.
- [ ] Basic events: start, update, interaction, trigger enter/exit, and animation cue.
- [ ] A scripting interface with logging, errors, reload, and controlled scene access.
- [ ] Timers, spawning, prefabs, scene transitions, and persistent game state.
- [ ] Simple AI states, perception, navigation, and behavior debugging.
- [ ] Inspect runtime state and pause/step through behavior during Play mode.
- [ ] **Research:** original entity classes, parameter blocks, logic graphs, and events.
- [ ] **Research:** which original behaviors need a compatibility layer or runtime code.

Start with a GSX-native behavior: a trigger opens a door and plays a sound.
Original EOT gameplay should execute through EdgeOfTimeRecomp. GSX-authored logic
and modifications to existing game behaviors need an explicit bridge into that
runtime, rather than assuming that asset loading alone provides execution.

## Cameras, cinematics, effects, and UI

- [ ] Scene cameras with lens, clipping, and viewport preview settings.
- [ ] Timeline tracks for transforms, camera cuts, animation, audio, and events.
- [ ] **Research:** original cutscene resources and event bindings.
- [ ] Particles, emitters, decals, trails, and effect resource inspection.
- [ ] Fog, sky, water, and other level-specific rendering systems as needed.
- [ ] Runtime text, fonts, sprites, UI layout, and input focus.
- [ ] Localization and language-dependent resource resolution.

## Reliability, diagnostics, and shipping

- [ ] Central undo/redo, dirty tracking, autosave, and recovery.
- [ ] Persistent logs with build identity, package/resource context, and errors.
- [ ] CPU/GPU timings, frame statistics, memory use, and loading diagnostics.
- [ ] Fixtures for package references, transforms, scenes, materials, and animations.
- [ ] Render tests for shared instances, depth, transparency, resize, and cleanup.
- [ ] Round-trip tests for every supported package edit.
- [ ] A small known scene used for visual and runtime regression checks.
- [ ] Editor and standalone scene-player builds from the same runtime code.
- [ ] Project packaging with dependency checks and asset preparation.
- [ ] Windows x64, Linux x64, and macOS ARM64 build/runtime checks.
- [ ] Tag-based releases with bundled dependencies and smoke-tested packages.

## Later scope to decide

- [ ] Networking, replication, multiplayer editing, and synchronization rules.
- [ ] Plugin interfaces and additional game-format importers.
- [ ] Additional rendering backends or platforms after profiling and compatibility
      requirements justify the work.

These are possible extensions, not prerequisites for the first usable level editor.

## Development order

1. Runtime bridge: launch, connect, controller play, pause, inspect, and resume.
2. Authoring scene: two model instances, transforms, save/load, and undo.
3. Runtime object mapping: inspect one existing object and apply a reversible edit.
4. Add a section while paused, with visible geometry and verified collision, then
   resume gameplay and test it without restarting.
5. Persist/reapply section layers, mount dependencies, and support reusable prefabs.
6. Expose runtime materials, animation, audio, lights, and effects in the inspector;
   extend GSX's independent previews where they help offline authoring.
7. Add logic/trigger editing, navigation, cinematics, and broader level import/export.
8. Integrate the runtime viewport and improve standalone project workflows.

## Reverse-engineering questions to answer

- [ ] How do level instances reference geometry, hierarchy, and material resources?
- [ ] Which assets are supplied by other packages or generated at runtime?
- [ ] How are bind poses, bone palettes, animation tracks, and root motion stored?
- [ ] How are shader constants, texture slots, techniques, and render passes bound?
- [ ] How are collision, navigation, triggers, and entity parameters represented?
- [ ] Which audio formats can we write while preserving game compatibility?
- [ ] Which parts of default.xex or reeot runtime output can answer these questions?

Request targeted disassembly or runtime captures when a specific feature reaches
one of these gaps. Keep small verified samples and record the findings alongside
the implementation.
