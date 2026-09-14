---
title: "feat: multi-plugin chain (Phase 1.D.3)"
type: feat
status: completed
date: 2026-04-28
origin: docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md
---

## Enhancement Summary

**Deepened on:** 2026-04-28
**Sections enhanced:** Proposed Solution, Technical Approach (Patch / PreparedGraph / Session / Persistence), Implementation Units (folded 9 → 6), Acceptance Criteria (added T–Z), Risk Analysis
**Research lanes used:** JUCE 8.0.4 framework docs · architecture review · performance review · simplicity review · pattern-recognition · spec-flow · agent-native parity

### Critical corrections (correctness / safety)

1. **Editor lifecycle on `replacePatch` swap was a use-after-free.** `EditorWindow`s hold raw `AudioPluginInstance*` pointing into `BypassableInstance` nodes. A whole-patch swap destroys those nodes the moment `swapPreparedGraph` lands. **Fix:** `replacePatch` closes all open editors *before* the swap commits. Spec'd in Unit 8 + AC-1.D.3-T.
2. **`AudioProcessorGraph::removeNode` is NOT destructive.** Returns `Node::Ptr` (a `ReferenceCountedObjectPtr<Node>`); inner `AudioProcessor` survives as long as the caller holds the ptr. The plan's Alt 6 "destructive removeNode" reasoning is corrected — reparenting still requires a JUCE patch (Node owns AudioProcessor via `unique_ptr` with no public extractor) but the rejection rationale is now factual.
3. **Drop the graph-wide `prepareToPlay` from incremental add.** JUCE's `NodeStates::applySettings` (juce_AudioProcessorGraph.cpp:404-484) auto-prepares only the *new* node via the topology-change rebuild. Calling `graph->prepareToPlay` after `addNode` would re-prepare every existing node, clobbering parameter state on plugins that reset on prepare (Kontakt, Spectrasonics, Pro-Q analyzer state).
4. **Use `UpdateKind::async` on intermediate batched mutations + `sync` on the last call.** `moveNode`'s `removeAllConnections + N×addConnection` currently triggers N full render-sequence rebuilds with default `UpdateKind::sync` — for a 10-plugin reorder, ~40 rebuilds. Async-then-sync collapses them into one. Specified per-op in the new Unit 2/4 approach.
5. **Re-read tail + runtime SR/block under the message-thread lock in `onAddResult`** — not from the worker-thread snapshot. Prevents a `moveNode` (synchronous, fires on the message thread between worker dispatch and worker return) from leaving the new node wired to a stale tail. AC-1.D.3-Z.
6. **`juce::SHA256` is not linked.** `pb3_session_core` doesn't depend on `juce_cryptography`. Switch the crash-loop marker to `juce::String::hashCode64()` over the concatenated identity string. Avoids new module dependency.
7. **`<PATCH schemaMajor="1">` namespace-collides with the future `.pdl` schema (master plan §4.5 reserves `schemaMajor="3"`).** Switch the Preferences root tag to `<ACTIVE_PATCH>` and drop the schema version attribute entirely for the 1.D.3 stub. Phase 2 owns schema negotiation.
8. **Linear-chain MIDI by default, not broadcast.** v2 parity: MIDI flows `midiInputNode → node[0] → node[1] → ... → midiOutputNode`, mirroring audio. The original "broadcast to every plugin" design dropped any plugin's MIDI output (e.g., arpeggiator → synth would be silent). Phase 3 canvas can rewire.

### Simplifications (drop / fold / defer)

9. **`Patch::Diff` struct dropped.** No caller reads it. Session reconciles inline if/when reuse helps.
10. **`PreparedGraph::patch` field dropped.** Session is the single source of truth for the active patch; PreparedGraph holds the live graph + per-node ref map only. One source of state, no drift invariant to maintain.
11. **`NodeId` simplified.** `using NodeId = int; constexpr NodeId graphIO = 0;` instead of strong-typedef + `<=>` + `std::hash` boilerplate. Type-safety theatre at this scale.
12. **Linear helpers moved out of `Patch`.** `makeLinearChain` / `appendLinearNode` / `moveNodeLinear` become private static helpers in `MainWindow.cpp` (or a tiny `ui/LinearLayout.h`). The `Patch` struct stays a pure topology record so Phase 3's canvas isn't fighting baked-in linear assumptions.
13. **Schema versioning attribute dropped.** Phase 2 introduces a real version scheme; coexisting two-version schemes is worse than zero.
14. **Migration helper dropped.** "First 1.D.3 launch starts blank; previous `activePluginDescription` is ignored" is a release note, not a code path. AC-1.D.3-N goes away. The single user affected (the developer) re-adds the plugin once.
15. **Units 9 → 6.** Unit 5 (delete PluginSlot, ~30 LOC of cleanup) folds into Unit 4. Unit 6 (crash-loop guard) folds into Unit 7 (App restore). Unit 9 (smoke + plan flip) is verification, not an implementation unit.

### New acceptance criteria from deepening (T–Z)

- **AC-1.D.3-T** `replacePatch` closes all open `EditorWindow`s before the swap commits; no editor outlives its underlying `BypassableInstance`.
- **AC-1.D.3-U** Synchronous ops (`removeNode`, `moveNode`, `setNodeBypass`, `setNodeCustomName`) bump `stateVersion_` while keeping `state_ == Idle`. Polling harnesses observe each mutation.
- **AC-1.D.3-V** Restore-on-launch deferred until audio device reports `sampleRate > 0`; if SR never arrives, restore is abandoned cleanly without clearing `activePatch`.
- **AC-1.D.3-W** PreferencesPersister writes `activePatch` after every patch-mutating op (success path). Diff-against-last-written to skip no-op writes.
- **AC-1.D.3-X** Restore rejects (and logs + clears `activePatch`) on any of: duplicate `NodeId`, `<CONN>` referencing absent node, cyclic connections.
- **AC-1.D.3-Y** Adding a heavy synth (Diva, Omnisphere, Serum) may produce one audible glitch on the *transition block*; documented limitation, not a regression. Pre-warm via worker-thread `processBlock(silence)` reduces but does not eliminate this.
- **AC-1.D.3-Z** `addPlugin`'s message-thread commit re-reads current patch tail and current runtime SR/block under the message-thread lock; stale worker-side snapshots are never used to compute connections or play config.

### New agent-native helpers

- `Session::getNodeIdsInOrder() -> std::vector<NodeId>` — cheap polling primitive.
- `Session::getNodeSnapshots() -> std::vector<NodeSnapshot{id, name, bypassed}>` — single-pass enumerator for harness scripts; avoids the heavyweight `getActivePatch()` copy.
- Pointer-stability rules added to `Session.h` doc-block: `getHostedPlugin(NodeId)` is stable across `moveNode` and `setNodeBypass`; invalidated by `removeNode`/`clearPatch`/`replacePatch`.
- Sync-vs-async observation rule: synchronous ops take effect before the call returns (no `waitForIdle` needed); async ops require `waitForIdle()` or a `getStateVersion()` delta.

### Renamed for honesty

- `Session::queuePatch(Patch)` → `Session::replacePatch(Patch)`. The op replaces; it doesn't merge. The honest name reduces "wait, doesn't this preserve state?" surprise. Coalescing semantics, mailbox shape, and state-machine flow unchanged.

# feat: multi-plugin chain (Phase 1.D.3)

## Overview

Phase 1.D.2 shipped a queued patch-switch state machine over a *single*
`session::PluginSlot` — the user can load one VST3 at a time, hear it,
quit-and-restart-and-it-comes-back. This phase generalises that single
slot into a *chain* of N plugin instances wired through a single
`juce::AudioProcessorGraph`, with the whole chain (the **patch**)
becoming the unit the rest of the app reasons about: persistence,
restore-on-launch, and (eventually) `.pdl` save/load all key off
`session::Patch`.

The user can:

- **Add** a plugin to the chain (instantiated on the worker thread; UI stays responsive).
- **Remove** a plugin from the chain.
- **Reorder** plugins (move up / move down — linear UI only this phase).
- **Bypass** any single plugin in the chain.
- **Edit** any plugin's GUI in its own window (multiple editors simultaneously).
- **Clear** the whole chain.
- **Restore** the entire chain on launch.

The engine is built to support **arbitrary topology** (branches, merges, sidechains)
so Phase 3's drag-to-connect canvas plugs into the existing engine without
re-engineering. Phase 1.D.3's UI exposes only the linear-chain subset to keep the
slice focused.

## Problem Statement

Manual smoke test on the user's MaxBook (post-1.D.2 merge):

> "It seems like it loads and VSTs load, but can only have one vst.
>  can't connect the plugins. not sure if its supposed to? The UI isn't
>  looking like a proper Pedalboard either."

The first half is exactly the gap — `PluginSlot` is by name and design
single-slot. The "proper Pedalboard" UI (drag-to-connect canvas, node cards
with input/output pins, the 19-token colour scheme) lives in Phase 3 of the
master rewrite plan and isn't in scope here.

But before Phase 3 can land, the engine needs to host a multi-plugin chain
and the data model needs to express patches. That's Phase 1.D.3. After this:

- Phase 2 layers `.pdl` save/load on top of the in-memory `Patch` type.
- Phase 3 layers the drag-to-connect canvas on top of the multi-node graph.
- Phase 4 wires MIDI/OSC mappings to nodes by `NodeId`.

So Phase 1.D.3 is the spine that gates Phase 2, Phase 3, and Phase 4.

## Proposed Solution

Introduce `session::Patch` — a **pure data** record of N nodes + their
connections + per-node bypass and custom name. Generalise
`session::buildPreparedGraph` from "build graph for one description" to
"build graph for whole patch". Expand `session::Session` to operate on a
patch instead of a single slot:

```cpp
class Session : public juce::ChangeBroadcaster {
public:
    // Whole-patch atomic swap (1.D.2 flow generalised). Renamed from
    // queuePatch for honesty: this REPLACES the live patch; it does not
    // merge. Existing plugin instances are destroyed; new ones replace
    // them. State preservation across replacePatch is Phase 2's concern
    // (Node::Ptr reparenting requires a JUCE patch).
    void replacePatch(Patch);
    void clearPatch();                                           // = replacePatch({})

    // Incremental ops — preserve existing live plugin instances.
    void addPlugin(juce::PluginDescription);
    void removeNode(NodeId);
    void moveNode(NodeId, int newIndex);                         // linear chain reorder
    void setNodeBypass(NodeId, bool);
    void setNodeCustomName(NodeId, juce::String);

    // Snapshot accessors.
    Patch                       getActivePatch() const;          // heavyweight copy
    std::vector<NodeId>         getNodeIdsInOrder() const noexcept;  // cheap polling
    struct NodeSnapshot { NodeId id; juce::String name; bool bypassed; };
    std::vector<NodeSnapshot>   getNodeSnapshots() const;        // single-pass enumeration
    bool                        hasNode(NodeId) const noexcept;
    juce::AudioPluginInstance*  getHostedPlugin(NodeId) const noexcept;
    bool                        isNodeBypassed(NodeId) const noexcept;

    // Preserved from 1.D.2.
    State         getState() const noexcept;
    Status        getStatus() const;
    std::uint64_t getStateVersion() const noexcept;
    bool          waitForIdle(int timeoutMs);
    void          dispatchPendingResult();
};
```

**Pointer / NodeId stability contract** (documented in `Session.h`):

- `getHostedPlugin(NodeId)` returns a stable pointer across `moveNode`,
  `setNodeBypass`, and `setNodeCustomName`. **Invalidated** by
  `removeNode(id)`, `clearPatch()`, `replacePatch(...)`. Agent harnesses
  and tests should re-fetch after those.
- Synchronous ops (`removeNode`, `moveNode`, `setNodeBypass`,
  `setNodeCustomName`) take effect *before the call returns*; no
  `waitForIdle` needed. Asynchronous ops (`addPlugin`, `replacePatch`,
  `clearPatch`) require `waitForIdle()` or a `getStateVersion()` delta
  to observe completion.
- `stateVersion_` bumps on **every** state transition AND every
  successful synchronous mutation, even when `state_` stays `Idle`
  (AC-1.D.3-U). Polling harnesses use it to detect change.

Two distinct mutation paths:

1. **Whole-patch swap (`queuePatch`, `clearPatch`)** — runs through the
   1.D.2 state machine. Worker thread builds the new graph from scratch
   (instantiating every plugin), message thread atomically swaps it via
   `engine::AudioGraphRuntime::swapPreparedGraph`. The audio-thread-side
   all-notes-off flush still applies. Plugin state is **not** preserved
   across whole-patch swaps in this phase (Phase 2 brings state blobs).

2. **Incremental mutation (`addPlugin`, `removeNode`, `moveNode`,
   `setNodeBypass`)** — preserves existing live plugin instances. JUCE's
   `juce::AudioProcessorGraph` is documented as message-thread-mutable
   while audio plays; we lean on that. Worker thread only participates
   in `addPlugin` (where instantiation is the slow part); `removeNode`
   / `moveNode` / `setNodeBypass` happen synchronously on the message
   thread.

`session::PluginSlot` is **deleted** — its role (hold one plugin's observer
pointers + name) was the one-slot version of what `Session` now does
directly with a `std::unordered_map<NodeId, NodeRefs>`. PreparedGraph and
buildPreparedGraph are reshaped to carry per-node refs.

## Technical Approach

### Data model

```cpp
// session/NodeId.h — new file (small)
namespace pedalboard3::session {

// Per-patch unique node identity. Integer matches master plan §4.5
// schema (`<NODE id="1">`). Generated by Patch::nextNodeId() as
// max(existing) + 1 — never reused even after delete-then-add cycles
// within a session. Regenerated on patch load (no inter-session
// stability needed — Phase 4 mappings reference NodeIds within the
// live patch only). Strong-typedef rejected during deepening as
// type-safety theatre at this scale.
using NodeId = int;
constexpr NodeId graphIO = 0;   // sentinel for graph audio/midi I/O endpoints

} // namespace pedalboard3::session
```

```cpp
// session/Patch.h — new file
namespace pedalboard3::session {

struct PatchNode {
    NodeId                  id;
    juce::PluginDescription desc;
    bool                    bypass{false};
    juce::String            customName;     // optional UI label override
};

struct PatchConnection {
    NodeId src;
    int    srcChannel;
    NodeId dst;
    int    dstChannel;

    // The sentinel `session::graphIO` (== 0) means "graph audio/midi
    // input" on the src side or "graph audio/midi output" on the dst
    // side. Channel index uses juce::AudioProcessorGraph::midiChannelIndex
    // (= 0x1000) for MIDI pins.
};

struct Patch {
    std::vector<PatchNode>       nodes;
    std::vector<PatchConnection> connections;
    juce::String                 name{"Untitled"};

    // max(existing node.id) + 1, or 1 if empty. Never returns an id
    // that's been used in this Patch's lifetime, even post-remove.
    NodeId nextNodeId() const noexcept;
};

} // namespace pedalboard3::session
```

`Patch` is a **pure topology record**. Linear-chain helpers
(`makeLinearChain`, `appendLinearNode`, `removeNodeAndRewireLinear`,
`moveNodeLinear`) live as private static helpers in `MainWindow.cpp` (or
a tiny `ui/LinearLayout.h`) — keeping `linearity` out of the data type
so Phase 3's canvas (which mutates `connections` directly) isn't fighting
baked-in linear assumptions. `Patch::Diff` was dropped during deepening
(no caller).

### `buildPreparedGraph` — multi-node generalisation

```cpp
// session/PreparedGraph.h — extended
struct PreparedGraph {
    std::unique_ptr<juce::AudioProcessorGraph> graph;

    struct NodeRefs {
        engine::BypassableInstance*      bypass{nullptr};
        juce::AudioPluginInstance*       hosted{nullptr};
        juce::AudioProcessorGraph::Node* graphNode{nullptr};   // for incremental ops
    };
    std::unordered_map<NodeId, NodeRefs> nodes;

    bool isValid() const noexcept { return graph != nullptr; }
};

// Builds a graph hosting all `patch.nodes`, with audio + MIDI in/out
// IO processors and the connections in `patch.connections`. Calls
// prepareToPlay. On any plugin instantiation failure: returns an empty
// PreparedGraph and `errorOut` carries a "<plugin name>: <reason>"
// line for the failed plugin (the rest are not built).
PreparedGraph buildPreparedGraph(engine::PluginHost& host,
                                 const Patch& patch,
                                 double sampleRate,
                                 int    blockSize,
                                 juce::String& errorOut);
```

Note: PreparedGraph **does not store the Patch** (deepening's C1
correction). `Session` owns the active `Patch` as a separate field; the
invariant "PreparedGraph.nodes map keys ≡ Session.activePatch.nodes ids"
is maintained on every mutation. Two-source-of-truth drift was the risk;
one source is the fix.

Internals: iterate `patch.nodes`, instantiate via `host.createInstance`,
wrap in `BypassableInstance`, addNode (each call uses
`UpdateKind::async` except the last, which uses `sync` — JUCE
collapses the topology rebuild into a single pass; verified via
`juce_AudioProcessorGraph.cpp:1857-1948`). Add audio in/out + midi
in/out nodes. Iterate `patch.connections`, addConnection (same async-
batched + sync-last pattern). Single `graph->prepareToPlay`. The pattern
matches master plan Appendix A.1.

**MIDI routing — linear chain (v2 parity).** `buildPreparedGraph` wires
MIDI as `midiInputNode → node[0].midi → node[1].midi → ... →
node[N-1].midi → midiOutputNode`, mirroring audio. JUCE's
`AudioProcessorGraph` does *not* implicitly broadcast MIDI; each plugin
sees only the explicit MIDI sources connected to its
`juce::AudioProcessorGraph::midiChannelIndex` (= 0x1000) input pin.
Linear chain matches Pedalboard 2 (`FilterGraph::midiChannelNumber =
0x1000`, master plan §4.5 line 245); broadcast was the original draft
but breaks MIDI-effect chains (arpeggiator → synth). Phase 3 canvas
lets users rewire arbitrary MIDI topology.

**Empty patch:** build a graph with `audioInputNode → audioOutputNode`
straight-through audio (per stereo channel) + `midiInputNode →
midiOutputNode` for MIDI pass-through. Matches Pedalboard 2's behaviour
when `<FILTERGRAPH>` is empty. Documented in the header.

### Session — request variant + dual queue

```cpp
namespace pedalboard3::session {

class Session : public juce::ChangeBroadcaster {
private:
    enum class RequestKind { LoadPatch, AddPlugin, RemoveNode, MoveNode };

    struct LoadPatchRequest  { Patch patch; };
    struct AddPluginRequest  { juce::PluginDescription desc; };
    struct RemoveNodeRequest { NodeId id; };
    struct MoveNodeRequest   { NodeId id; int newIndex; };

    using Request = std::variant<LoadPatchRequest, AddPluginRequest,
                                 RemoveNodeRequest, MoveNodeRequest>;

    // Mailbox: latest-wins for LoadPatch (most disruptive).
    // FIFO queue for AddPlugin (each is independent).
    // RemoveNode / MoveNode are synchronous on message thread — never
    // queued.
    std::optional<LoadPatchRequest> latestPatchRequest_;   // depth-1 mailbox
    std::deque<AddPluginRequest>    addQueue_;             // bounded FIFO

    // ... rest of Session state from 1.D.2
};

} // namespace pedalboard3::session
```

The worker thread drains `latestPatchRequest_` first if present (because a
new whole-patch swap supersedes any pending adds). Then drains `addQueue_`
one entry per signal.

**Coalescing rules:**

- `queuePatch` arriving while `addQueue_` has entries → discard the add
  queue (the new patch supersedes).
- Two `queuePatch` calls in flight → second wins (depth-1 latest-wins, as
  in 1.D.2).
- Many `addPlugin` calls in flight → all queued FIFO, processed serially.
  Bound `addQueue_` at 16 to prevent runaway from a stuck UI.

### Incremental mutation paths

#### `addPlugin` (worker + message thread)

```
Worker:
  1. Pop AddPluginRequest from addQueue_.
  2. Snapshot SR/block from runtime (best-effort; re-checked on commit).
  3. host.createInstance(desc) — slow; off the message thread.
  4. Wrap in BypassableInstance.
  5. Pre-warm: bypass.prepareToPlay(SR, block) AND one
     bypass.processBlock(silence, emptyMidi) on the worker. Pays the
     plugin's first-block cost off the audio path → reduces transition-
     block glitch with heavy synths (AC-1.D.3-Y mitigation).
  6. Deposit BuildResult{ instance, desc, errorOut } into result_.
  7. resultUpdater_->triggerAsyncUpdate().

Message thread (Session::onAddResult):
  1. Read BuildResult.
  2. Coalesce check: if a replacePatch is now pending, drop this
     add result (RAII destroys the prepared instance on this thread).
  3. RE-READ activePatch_ tail and runtime SR/block under the message-
     thread lock (AC-1.D.3-Z) — the snapshot the worker had may be
     stale because synchronous moveNode / removeNode could have run
     while the worker was building.
  4. Allocate a fresh NodeId from activePatch_ (max+1, never reused).
  5. graph->addNode(BypassableInstance, UpdateKind::async).
  6. Recompute linear-chain connections (insert this node at end).
  7. graph->removeAllConnections (UpdateKind::async) +
     graph->addConnection × N (last call uses UpdateKind::sync —
     JUCE collapses all the intermediate topology changes into a single
     RenderSequence rebuild via NodeStates::applySettings).
  8. **DO NOT** call graph->setPlayConfigDetails / prepareToPlay
     unless SR/block actually changed since the worker snapshot. JUCE
     auto-prepares the new node via the topology-rebuild path; calling
     graph->prepareToPlay would re-prepare every existing node and
     clobber parameter state on plugins that reset on prepare (Kontakt,
     Pro-Q analyzer, etc.).
  9. Update preparedGraph.nodes[NodeId] = NodeRefs{ bypass, hosted,
     graphNode }.
 10. Update activePatch_ (append node + recompute connections).
 11. Bump stateVersion_, publishStateChange(Idle) (only if no further
     work pending), sendChangeMessage.
```

#### `removeNode` (synchronous, message thread)

```
1. Look up NodeRefs[NodeId]. Bail if absent.
2. Close any open EditorWindow keyed by this NodeId (UI side; happens
   via the change-message after Session updates state).
3. graph->removeNode(graphNode->nodeID, UpdateKind::async)
   — returns Node::Ptr (NOT destructive; the BypassableInstance survives
   as long as the Ptr is held). Let the Ptr fall off scope here so the
   plugin destructs on the message thread.
4. Recompute linear-chain connections for the survivors:
   removeAllConnections(UpdateKind::async) + addConnection × N
   (last call UpdateKind::sync to commit the rebuild).
5. preparedGraph.nodes.erase(NodeId).
6. activePatch_.nodes.erase(...) + recompute activePatch_.connections.
7. Bump stateVersion_, sendChangeMessage.

Note: the doomed node receives no all-notes-off in 1.D.3 (held notes
play out via the plugin's release envelope). Documented limitation.
```

#### `moveNode` (synchronous, message thread)

```
1. Early-return if newIndex == currentIndex (no-op).
2. Reorder activePatch_.nodes (vector reorder).
3. Recompute activePatch_.connections as the new linear chain.
4. graph->removeAllConnections(UpdateKind::async).
5. graph->addConnection × N (last call UpdateKind::sync).
6. graphNodes themselves are untouched — plugin instances + state intact.
7. Bump stateVersion_, sendChangeMessage.

Caveat: `removeAllConnections + addConnection` is not atomic w.r.t.
the audio thread; there's a brief window where the graph has neither
the old nor the new connections. Worst case is one block of silence
on the moveNode block. Acceptable for 1.D.3; documented under
AC-1.D.3-D.
```

#### `setNodeBypass` (synchronous, message thread)

```
1. nodeRefs = preparedGraph.nodes[NodeId]. Bail if absent.
2. nodeRefs.bypass->setBypass(shouldBypass);  // atomic, lock-free
3. activePatch_.nodes[i].bypass = shouldBypass;  // mirror
4. Bump stateVersion_, sendChangeMessage.
```

### State machine

The Idle/Building/Swapping states from 1.D.2 stay. Building covers both
"worker is building a whole patch" and "worker is instantiating a plugin
for an Add". Swapping is the brief message-thread-side commit window for
both whole-patch swaps and incremental adds.

Synchronous ops (`removeNode`, `moveNode`, `setNodeBypass`) don't touch
state_ — they update state in-place and fire a change message.

### Engine layer — AudioGraphRuntime untouched

`engine::AudioGraphRuntime` doesn't change. The whole-patch swap path
goes through the existing `swapPreparedGraph` (with the
all-notes-off / pending_ atomic handshake from 1.D.2).

Incremental ops mutate the live `juce::AudioProcessorGraph` directly via
its message-thread-mutation API. The audio thread reads JUCE's render
plan as usual; JUCE handles the visibility internals.

**Open question (deferred to implementation):** does
`AudioProcessorGraph::removeNode` need an explicit `UpdateKind` argument
to keep the audio thread consistent? Default is fine for our case
(per JUCE docs, the graph rebuilds its render plan on the next
processBlock automatically). Verify with a stress test that adds and
removes 20 plugins in rapid succession under live audio.

### Persistence — Preferences active-patch

Replace `activePluginDescription` (single XML element) with
`activePatch` (the patch as XML, distinct root tag `<ACTIVE_PATCH>` to
avoid namespace-clashing with the future `.pdl` `<PATCH
schemaMajor="3">` per master plan §4.5):

```xml
<ACTIVE_PATCH name="Untitled">
  <NODES>
    <NODE id="1" bypass="false" customName="">
      <IDENTITY format="VST3" name="Pro-Q 3" file="..." uniqueId="..."/>
    </NODE>
    <NODE id="2" bypass="false" customName="">
      <IDENTITY format="VST3" name="Compressor" file="..." uniqueId="..."/>
    </NODE>
  </NODES>
  <CONNECTIONS>
    <CONN src="0" srcChan="0" dst="1" dstChan="0"/>
    <CONN src="0" srcChan="1" dst="1" dstChan="1"/>
    <CONN src="1" srcChan="0" dst="2" dstChan="0"/>
    <CONN src="1" srcChan="1" dst="2" dstChan="1"/>
    <CONN src="2" srcChan="0" dst="0" dstChan="0"/>
    <CONN src="2" srcChan="1" dst="0" dstChan="1"/>
    <CONN src="0" srcChan="4096" dst="1" dstChan="4096"/>  <!-- MIDI: in→1 -->
    <CONN src="1" srcChan="4096" dst="2" dstChan="4096"/>  <!-- MIDI: 1→2 -->
    <CONN src="2" srcChan="4096" dst="0" dstChan="4096"/>  <!-- MIDI: 2→out -->
  </CONNECTIONS>
</ACTIVE_PATCH>
```

No `schemaMajor`/`schemaMinor` on the 1.D.3 stub. Phase 2's
`PdlSerializer` introduces real schema versioning; coexisting two
versioning schemes is worse than zero (deepening's simplicity finding).

`Preferences` API:

```cpp
std::unique_ptr<Patch> getActivePatch();
void                   setActivePatch(const Patch&);
void                   clearActivePatch();
```

**No migration helper.** First 1.D.3 launch starts with a blank chain;
the developer (the only user with a 1.D.2-saved plugin) re-adds it
once. Release-note item, not a code path.

**Restore validation** rejects the saved patch and clears
`activePatch` if any of the following fail (AC-1.D.3-X):
- Duplicate `NodeId` in `<NODE>` entries.
- A `<CONN>` references a NodeId not in the `<NODES>` list.
- The connection graph has a cycle (impossible from the linear UI but
  possible from a hand-edited prefs file or a future Phase-3
  multi-output patch loaded by an older binary).

**Crash-loop marker.** Replace 1.D.2's raw `fileOrIdentifier` string
with a hash of the patch's identity:
`juce::String::hashCode64(concatenated fileOrIdentifiers)`. The hashed
form is stable per-patch and survives across launches without leaking
filesystem paths into the marker file. **`juce::SHA256` was rejected**
during deepening because `pb3_session_core` doesn't link
`juce_cryptography`; `String::hashCode64` is sufficient for crash-loop
detection (collisions are not a security concern here).

### Restore-on-launch

`App::restoreActivePluginIfAny` becomes `App::restoreActivePatchIfAny`:

1. **Wait for audio device readiness** (AC-1.D.3-V): if
   `engine_->getRuntime()->getSampleRate() == 0`, register a one-shot
   listener on the AudioDeviceManager and defer the restore until SR > 0.
   If SR never arrives (headless / no device), abandon restore cleanly
   *without clearing `activePatch`* — the user gets back to it next
   launch when a device is available.
2. Read `activePatch`.
3. **Validate structure** (AC-1.D.3-X): reject + clear on duplicate
   NodeIds, dangling CONN, or cycles.
4. For each PatchNode in the saved patch, cross-check against
   `KnownPluginList` (AC-1.D.2-L generalised to per-node):
   - If any node's plugin is not in the list, refuse the whole patch
     (no partial-chain loading in 1.D.3). Log + clear `activePatch`.
5. Cross-check the crash-loop marker: if it matches the patch's
   identity hash (`String::hashCode64` over concatenated
   fileOrIdentifiers), skip + clear.
6. Tag the marker, then `session->replacePatch(savedPatch)`.
7. PreferencesPersister clears the marker on Idle-with-success.

### MainWindow chain UI

Replace the single-slot row with a vertical **chain panel**:

```
+------------------------------------------------------------+
| Pedalboard 3                                              |
| Phase 1.D.3 — multi-plugin chain                          |
|                                                            |
| [Plugin Picker ▼] [Add to chain]   Status: Idle           |
|                                                            |
| Chain (3 plugins):                                         |
|  ┌────────────────────────────────────────────────────┐  |
|  │ 1. Pro-Q 3  [VST3]   [Bypass ☐] [Edit] [↑] [↓] [×] │  |
|  │ 2. Compressor [VST3] [Bypass ☐] [Edit] [↑] [↓] [×] │  |
|  │ 3. Limiter   [VST3]  [Bypass ☐] [Edit] [↑] [↓] [×] │  |
|  └────────────────────────────────────────────────────┘  |
|                                                            |
| [Clear all]                                                |
|                                                            |
| [Audio Settings] [Test Tone] [Plugin List]                 |
+------------------------------------------------------------+
```

Buttons per row:
- **Bypass** — `Session::setNodeBypass(NodeId, !current)`.
- **Edit** — opens an `EditorWindow` for that node; multiple windows
  can be open at once. `MainWindow` keeps a `std::unordered_map<NodeId,
  std::unique_ptr<EditorWindow>>` so closing one doesn't affect others.
- **↑** — `Session::moveNode(NodeId, currentIndex - 1)`. Disabled when
  index == 0.
- **↓** — `Session::moveNode(NodeId, currentIndex + 1)`. Disabled when
  index == lastIndex.
- **×** — `Session::removeNode(NodeId)` + close that node's
  `EditorWindow` if open.

A single `juce::Viewport` wraps the chain so long chains scroll.

### Test Tone & Plugin List dialogs — unchanged

Keep the existing diagnostic strip as-is. Test Tone still bypasses
`Session` (it's a graph-level diagnostic that swaps in a tone graph
without touching the patch state machine).

### Implementation Units

> 6 units (folded from 9 during deepening). Phase A units (1, 2, 3) can
> land in parallel as they touch non-overlapping files. Phase B (4, 5)
> depends on Phase A. Phase C (6) depends on B. Verification (smoke +
> integration tests) happens inside the relevant units, not as a
> separate unit.

#### Unit 1 — `Patch` data type + `NodeId`

- **Goal:** introduce the data model. Pure value types; no engine deps.
- **Files:**
  - `app-v3/src/session/NodeId.h` (new, ~30 LOC)
  - `app-v3/src/session/Patch.h` / `.cpp` (new, ~150 LOC)
  - `app-v3/src/session/CMakeLists.txt`
  - `app-v3/tests/unit/session/Patch_test.cpp` (new)
- **Approach:** struct definitions; helpers for linear-chain shape
  (`makeLinearChain`, `appendLinearNode`, `moveNodeLinear`,
  `removeNodeAndRewireLinear`); diff-against-previous (`Patch::diffAgainst`).
- **Patterns to follow:** existing `Preferences.h` doc-comment style;
  `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` (none; these are value
  types, leave unmacro'd).
- **Execution note:** test-first for the linear-chain helpers and diff.
- **Test scenarios:**
  - `nextNodeId()` on empty / 1-node / N-node returns max+1.
  - `makeLinearChain({A, B, C})` → 3 nodes, 8 audio connections (in→A,
    A→B, B→C, C→out — per stereo channel).
  - `appendLinearNode` retains existing connections + adds 4 new.
  - `removeNodeAndRewireLinear` of middle node removes 4 connections,
    adds 2 (predecessor → successor).
  - `moveNodeLinear` of last to first: connections fully recomputed.
  - `diffAgainst` correctly classifies kept/added/removed.
- **Verification:** all Patch_test cases pass; no engine deps in the .h.

#### Unit 2 — `PreparedGraph` multi-node + `buildPreparedGraph` rewrite

- **Goal:** make the existing builder accept a Patch instead of a
  single PluginDescription.
- **Files:**
  - `app-v3/src/session/PreparedGraph.h` / `.cpp` (significant rewrite)
  - `app-v3/tests/unit/session/PreparedGraph_test.cpp` (extend)
- **Approach:**
  ```cpp
  PreparedGraph buildPreparedGraph(engine::PluginHost& host,
                                   const Patch& patch,
                                   double sampleRate,
                                   int    blockSize,
                                   juce::String& errorOut);
  ```
  Iterate `patch.nodes`, instantiate, wrap, addNode. Add audio in/out +
  midi in/out IO processors. Iterate `patch.connections`, addConnection
  with the sentinel-NodeId (0) mapping to graphIO node IDs.
  prepareToPlay. Populate `nodes` map. Populate `patch` field.

  Empty-patch case: build graph with audio in → audio out (passthrough),
  no plugin nodes.

- **Patterns to follow:** existing `PreparedGraph.cpp:14-58`; the master
  plan Appendix A.1 PatchBuilder skeleton.
- **Execution note:** test-first.
- **Test scenarios:**
  - Empty patch → valid PreparedGraph with passthrough connections.
  - 1-node patch with bogus desc → empty PreparedGraph + errorOut.
  - N-node patch where one fails → empty PreparedGraph + errorOut names
    the failing node.
  - Existing 1.D.2 PreparedGraph_test.cpp scenarios still pass when
    callers wrap a single PluginDescription in `Patch::makeLinearChain({desc})`.
- **Verification:** all PreparedGraph_test cases pass; no behaviour
  regression for the single-plugin case.

#### Unit 3 — Migrate Preferences from `activePluginDescription` to `activePatch`

- **Goal:** persist the whole patch; one-shot migration from the
  1.D.2 single-plugin key.
- **Files:**
  - `app-v3/src/session/Preferences.h` / `.cpp`
  - `app-v3/tests/unit/session/Preferences_test.cpp` (extend)
- **Approach:**
  ```cpp
  std::unique_ptr<Patch> getActivePatch();
  void                   setActivePatch(const Patch&);
  void                   clearActivePatch();
  ```
  Patch <-> XML serialise via the schema sketched in `Persistence`
  above. Migration helper:
  ```cpp
  // If activePluginDescription exists and activePatch doesn't:
  //   read pluginDescription, build Patch::makeLinearChain({desc}),
  //   write activePatch, remove activePluginDescription.
  void migrateActivePluginDescription();
  ```
  Called once at the start of `Preferences::getActivePatch` (or from
  `App::initialise`). Idempotent.
- **Patterns to follow:** existing
  `Preferences::{get,set,clear}ActivePluginDescription` (1.D.2).
- **Execution note:** test-first.
- **Test scenarios:**
  - First-run: `getActivePatch` returns nullptr.
  - Round-trip: setActivePatch(P) → getActivePatch returns equivalent P.
  - Persistence across instance reconstruction.
  - Malformed XML returns nullptr.
  - Migration: write `activePluginDescription` directly via
    `getStore()->setValue`; call `migrateActivePluginDescription`;
    expect `activePatch` populated, `activePluginDescription` removed.
  - XXE regression test as in 1.D.2 (Preferences XML stays string-overload-only).
- **Verification:** all Preferences_test cases pass.

#### Unit 4 — Session API expansion + dual-queue worker

- **Goal:** evolve Session from one-slot to many-node.
- **Files:**
  - `app-v3/src/session/Session.h` / `.cpp`
  - `app-v3/tests/unit/session/Session_test.cpp` (significant rewrite)
- **Approach:** as described in `Session — request variant + dual queue`.
  Worker drains `latestPatchRequest_` first, then `addQueue_`. Result
  variant carries either a `PreparedGraph` (whole-patch swap) or a
  single `BypassableInstance` + `PluginDescription` (incremental add).
- **Patterns to follow:** 1.D.2 worker + AsyncUpdater pattern;
  `dispatchPendingResult` test seam.
- **Execution note:** test-first.
- **Test scenarios:**
  1. queuePatch with empty Patch → state Idle, hasNode is false for any id.
  2. queuePatch with 3-node patch → state walks Idle → Building → Idle,
     all 3 nodes reachable via getHostedPlugin (in test: BuildFn returns
     a non-null fake processor; getHostedPlugin should return null since
     fakes aren't AudioPluginInstance — but test asserts hasNode true).
  3. addPlugin → state Idle → Building → Idle, patch grows by 1.
  4. addPlugin × 5 in rapid succession → all 5 land, FIFO order.
  5. queuePatch arriving during addPlugin → addPlugin result discarded,
     queuePatch wins.
  6. removeNode mid-chain → patch shrinks by 1, surviving nodes intact.
  7. moveNode swaps positions → patch.nodes order matches request,
     connections recomputed.
  8. setNodeBypass → corresponding BypassableInstance::setBypass called,
     patch.nodes[i].bypass mirrors.
  9. clearPatch → equivalent to queuePatch({}).
- **Verification:** all Session_test cases pass on linux-ci; no
  thread-sanitizer warnings if available.

#### Unit 5 — App restore-on-launch + crash-loop marker (folded from old Units 5+6+7)

- **Goal:** delete `PluginSlot`; rewire `App` for patch-level restore;
  generalise crash-loop marker to patches.
- **Files:**
  - `app-v3/src/session/PluginSlot.h` / `.cpp` — **delete**.
  - `app-v3/tests/unit/session/PluginSlot_test.cpp` — **delete**.
  - `app-v3/src/session/CMakeLists.txt` — remove `PluginSlot.{h,cpp}` from
    `add_library` AND `pb3_apply_strict_warnings`.
  - `app-v3/tests/CMakeLists.txt` — remove `PluginSlot_test.cpp`.
  - `app-v3/src/AppContext.h` — drop the `session::PluginSlot& pluginSlot;`
    field. **Migrate the doc-block at lines 23–32**: remove the
    "Phase 1.D.1 added: session::PluginSlot" line; add
    "Phase 1.D.3: Session API expanded to multi-node patch (PluginSlot
    retired)".
  - `app-v3/src/App.h` / `.cpp` — drop the `pluginSlot` unique_ptr
    member; rewire restore as `restoreActivePatchIfAny` per the
    "Restore-on-launch" section above (defer-until-SR-ready, structural
    validation, per-node KnownPluginList cross-check, crash-loop hash).
  - `app-v3/src/session/PreferencesPersister.h` / `.cpp` — change marker
    contents from raw `fileOrIdentifier` to
    `juce::String::hashCode64()` over the concatenated identity string.
    Replace the listener's `getActiveDescription()` call with
    `getActivePatch()`; persistence write happens after every successful
    patch-mutating op (AC-1.D.3-W).
- **Approach:** mostly mechanical rewiring; the load logic is in the
  per-section text above. Construction order in `App::initialise`:
  preferences → engine → pluginHost → session → persister → context →
  mainWindow → restoreActivePatchIfAny.
- **Test scenarios:**
  1. Restore with valid 3-node patch, all in KnownPluginList → chain
     loaded.
  2. Restore where one node missing from list → patch refused, cleared.
  3. Restore where structure corrupt (dup NodeId / dangling CONN /
     cycle) → patch refused, cleared (AC-1.D.3-X).
  4. Restore deferred until SR > 0 (AC-1.D.3-V): construct App with no
     device; call `restoreActivePatchIfAny`; assert no worker dispatch.
     Then attach a device; assert worker dispatched.
  5. Crash-loop marker hash collision — write marker, attempt restore
     of same patch → refused; clear marker; restore again → succeeds.
- **Verification:** clean build; `grep -r PluginSlot app-v3/src
  app-v3/tests` returns nothing; macOS smoke — launch with chain
  saved, all nodes reload.

#### Unit 6 — MainWindow chain panel

- **Goal:** rewrite the slot row as a vertical chain.
- **Files:**
  - `app-v3/src/ui/MainWindow.cpp` (significant rewrite)
  - `app-v3/src/ui/MainWindow.h` (only if a member needs adding)
- **Approach:**
  - `RootComponent` keeps its KnownPluginList listener + Session listener.
  - Replaces the `pluginCombo_ + loadButton_ + clearButton_ + bypassButton_
    + editButton_` row with:
    - Top: `pluginCombo_` + `addButton_` (calls
      `ctx_.session.addPlugin(desc)`).
    - Middle: a `juce::Viewport` containing a custom `ChainListComponent`
      that lays out N `ChainRowComponent`s vertically. Each row holds:
      label, bypass toggle, edit button, up/down buttons, remove button.
    - Bottom: `clearButton_` (calls `ctx_.session.clearPatch()`).
  - `ChainRowComponent` constructor takes `(NodeId, AppContext&,
    onEditOpen)`. It listens to Session's change message to refresh
    its bypass state and disable buttons during state != Idle.
  - `EditorWindow` becomes per-NodeId; map kept on RootComponent.
- **Patterns to follow:** existing `RootComponent` ctor + listener
  pattern; existing `EditorWindow` inner class.
- **Execution note:** UI work; verify visually on macOS smoke.
- **Verification:**
  - Chain renders; multiple plugins visible.
  - Add/remove/move buttons mutate the chain as expected.
  - Multiple Edit windows open simultaneously.
  - Status label shows state from Session::getStatus().

### Implementation Phases

#### Phase A — primitives (Units 1, 2, 3)

Independent file-touching: `Patch` data type, `PreparedGraph` rewrite,
`Preferences::activePatch` round-trip. Can fan out to parallel
subagents.

#### Phase B — Session + App wiring (Units 4, 5)

Session API expansion + worker (Unit 4) + PluginSlot deletion + App
restore + Persister rewire (Unit 5). Depends on Phase A. Sequence:
4 → 5.

#### Phase C — UI (Unit 6)

MainWindow chain-panel rewrite + integration tests + smoke. Depends on
Phase B.

**Verification of the deliverable** is integrated into Units 4
(Session_test scenarios), 5 (App restore tests + macOS smoke), 6 (UI
smoke + harness integration test). No separate "verification unit".

### Validation discipline

Per `feedback_build_validation.md`: push to CI; do not run local
`cmake --build`/`ctest`. Per `feedback_clang_format_split_hoist.md`:
hoist named locals when clang-format-18 fights long calls (likely
hot spots: `Session::handleAddResult` worker callback, the
`ChainListComponent::resized` button row math).

Per the new `feedback_git_remote_ops.md`: agent may push to
`feat/multi-plugin-chain` (or whatever the branch is named) directly.
User keeps control of merging to main.

## Alternative Approaches Considered

### Alt 1 — Keep `PluginSlot` as a per-node primitive

`Session` would own `std::vector<PluginSlot>`. Each PluginSlot keeps its
single-plugin observer pointers + name. **Rejected.** PluginSlot's
contract is "this is THE one slot"; a vector of them is a misnomer. The
clean refactor names the concept correctly: a `Patch` is the set of
plugins, and Session owns it.

### Alt 2 — Always rebuild the graph on every mutation

Every addPlugin / removeNode / moveNode triggers a whole-patch swap
through the worker. **Rejected.** Whole-patch swap re-instantiates every
plugin, which would lose all parameter state on every "add a reverb"
click. The user expects the existing chain's plugins to stay alive when
they add a new one. Differential mutation (incremental add/remove via
JUCE's graph mutation API) preserves state.

### Alt 3 — Branching topology UI in 1.D.3

Skip the linear-only UI and ship the drag-to-connect canvas now.
**Rejected as scope creep.** The canvas (`PluginFieldView`,
`PluginNodeView`, `PluginConnectionView`, Bezier drawing, hit-testing,
lasso-select) is master plan Phase 3 (4-6 weeks). 1.D.3 is the
*engine-side* of multi-plugin support. Linear UI is the minimum
viable shape that exposes the new engine to users.

### Alt 4 — `juce::Uuid` NodeIds

Globally-unique 16-byte ids. **Rejected.** Master plan §4.5 schema uses
integer ids (`<NODE id="1">`). Per-patch uniqueness is enough for Phase
1.D.3, Phase 2 (PdlSerializer), and Phase 4 (mapping by NodeId in the
current patch). uuid would force schema deviation and noisy logs.

### Alt 5 — Stick with the depth-1 mailbox; don't add a FIFO for adds

Force `addPlugin` to coalesce (latest-wins). **Rejected.** Add operations
are additive — clicking "add Serum" then "add Vital" should produce two
plugins, not one. FIFO with bound 16 is the right shape; coalescing
loses user intent.

### Alt 6 — Preserve plugin state across whole-patch swap

When `replacePatch` finds nodes shared between old and new patches (same
PluginDescription), reparent the live BypassableInstance from the old
graph into the new graph instead of re-instantiating. **Rejected for
1.D.3** but kept in mind for Phase 2.

**Corrected during deepening:** the original "removeNode is destructive"
reasoning was wrong. JUCE 8.0.4's
`AudioProcessorGraph::removeNode(NodeID, UpdateKind)` returns
`Node::Ptr` (a `ReferenceCountedObjectPtr<Node>`); the inner
`AudioProcessor` survives as long as the caller holds the ptr. **However**,
the inner `AudioProcessor` is held by `std::unique_ptr<AudioProcessor>`
inside `Node` with no public extractor — so reparenting via `Node::Ptr`
keeps the node alive past `removeNode` (e.g., to defer destruction off
the audio path) but does **not** enable hot-handoff to a different
graph. Phase 2 reparenting still requires either a JUCE patch or a
`MemoryBlock` state-blob round-trip via `getStateInformation`/
`setStateInformation`. Phase 2's PdlSerializer will own state-blob
preservation as a separate concern.

## System-Wide Impact

### Interaction graph

When a user clicks "Add to chain" on a 2-second-instantiation synth
while two existing plugins are loaded:

1. UI button click on message thread → `Session::addPlugin(desc)`.
2. Session pushes `AddPluginRequest{desc}` onto `addQueue_` (FIFO),
   transitions state Idle → Building if currently Idle, signals worker.
3. MainWindow's `changeListenerCallback` runs on message thread, sees
   `Session::State::Building`, disables Add/Remove/Move buttons on all
   chain rows, updates the status label.
4. Worker thread: pops request, snapshots SR/block, calls
   `host_.createInstance(desc)` (the 2-second part — UI stays
   responsive on the message thread), wraps in BypassableInstance,
   deposits BuildResult, triggerAsyncUpdate.
5. Message thread runs `Session::onAddResult`: if no newer `queuePatch`
   intervened, allocates fresh NodeId, calls
   `graph->addNode(BypassableInstance)`, re-wires linear-chain
   connections (removes the existing tail→output edges, adds
   tail→newNode and newNode→output edges), updates patch + nodes map,
   transitions Idle, sendChangeMessage.
6. MainWindow's listener fires again: sees State::Idle, reads the new
   patch, rebuilds the chain rows so the new plugin appears as row N+1.
7. PreferencesPersister's listener fires: sees Idle + activeDescription
   has a patch (we extend its model to a patch), writes the new
   `activePatch` XML to Preferences.

When a user clicks "Move ↑" on row 3:

1. `Session::moveNode(NodeId_3, 1)` — synchronous on message thread.
2. Patch.nodes reorder; patch.connections recompute.
3. graph->removeAllConnections + addConnection per new connection.
4. sendChangeMessage.
5. MainWindow listener re-renders rows in new order.

When the user quits:

1. `App::shutdown` → flush Preferences → reset MainWindow → reset context →
   reset persister → reset Session (cancels AsyncUpdater + joins worker
   + drops orphaned BuildResult) → reset PluginHost → reset AudioEngine.
2. The whole AudioProcessorGraph is destroyed; every BypassableInstance
   + plugin instance dtors run on the message thread.

### Error & failure propagation

- **Plugin instantiation failure during `addPlugin`:** worker returns
  empty `BypassableInstance`, errorOut populated. `Session::onAddResult`
  sees the empty instance, sets `lastError_`, leaves the chain
  unchanged, transitions Idle. UI shows "Add failed: <reason>" in the
  status label.
- **Plugin instantiation failure during `queuePatch`:** worker's
  `buildPreparedGraph` returns invalid PreparedGraph, errorOut names
  the offending node. Session leaves the previous patch live; UI shows
  the error.
- **Incremental graph mutation failure on the message thread** (e.g.,
  `addNode` returns null, `addConnection` rejects an invalid pin): rare;
  log + revert (don't update patch); send a change message so UI
  re-syncs.
- **Plugin crash on the audio thread** during normal processing:
  caught by Phase 1.B's SEH/signal handlers (not in scope here);
  `BypassableInstance::dead_` flag goes true; the node emits silence;
  the rest of the chain keeps running.
- **Restore-on-launch with one missing plugin:** whole-patch refused;
  `activePatch` cleared; user sees an empty chain. (Alternative: load
  the partial chain. Deferred to v3.1; clean failure is simpler.)

### State lifecycle risks

- **NodeId collision after reorder + remove + add:** no — `nextNodeId`
  always returns max+1, so even after removes, new ids never collide
  with extant ones.
- **EditorWindow held open for a removed node:** MainWindow listens to
  Session changes; when a node disappears from the patch, the
  corresponding EditorWindow is closed (mapped by NodeId, removed on
  patch diff).
- **Stale BypassableInstance pointer after queuePatch:** Session's
  `nodes` map is rebuilt in lockstep with the new graph. The old map's
  pointers refer to the destroyed BypassableInstances; UI must not
  hold them across a queuePatch transition. ChainRowComponent only
  reads via `Session::getHostedPlugin(NodeId)` which goes through the
  current map.
- **addPlugin queue overflow:** bounded at 16; over-budget calls log
  + drop with an error. Won't happen in normal use.
- **Worker mid-build at queuePatch arrival:** result_ holds an in-
  progress PreparedGraph; coalescing drops it; RAII destroys the
  half-built graph + plugin instances on the message thread.
- **Plugin's editor window holds a BypassableInstance pointer the
  user is about to delete:** MainWindow closes editor windows BEFORE
  calling Session::removeNode; the BypassableInstance dtor runs after
  the editor is gone.

### API surface parity

- `Session::queueLoad(PluginDescription)` (1.D.2) becomes deprecated
  / removed. Callers (just MainWindow's onLoadClicked) migrate to
  `Session::addPlugin` (or `queuePatch(makeLinearChain({desc}))` for
  whole-replacement).
- `Session::queueUnload()` becomes `Session::clearPatch()`.
- `Session::isLoaded()` / `getLoadedName()` / `getActiveDescription()`
  — single-slot accessors; removed. Replaced with patch-scoped
  accessors.
- `Session::Status` — gains `int nodeCount`, drops `loadedName` (which
  is a per-node concern now).
- `PluginSlot` — entire type deleted (Unit 5).
- `AppContext` — drops `pluginSlot` field.

### Integration test scenarios

1. **Add 5 plugins, then move + remove + add** — assert final patch
   shape via Session::getActivePatch().
2. **Restore-on-launch round-trip** — save 3-node patch via Preferences;
   reconstruct App; assert chain reloaded with same node count and
   linear connections.
3. **Migration from 1.D.2 single-plugin Preferences** — pre-seed
   `activePluginDescription`; on first 1.D.3 launch, assert patch
   restored with 1 node, old key removed.
4. **Coalescing** — fire 10 addPlugin then 1 queuePatch; assert
   queuePatch wins, addPlugins discarded.
5. **State preservation across moveNode** — load 3 plugins, set a
   parameter on plugin 2, moveNode plugin 2 to position 1; assert the
   parameter value is unchanged on the now-row-1 plugin.
6. **Plugin-fault recovery on whole-patch swap** — queuePatch with one
   bogus desc; assert previous patch live, error reported, no
   half-loaded chain.

## Acceptance Criteria

### Functional Requirements

- [ ] **AC-1.D.3-A** `queuePatch(N-node)` builds + swaps; existing
  patch (if any) is replaced entirely. All-notes-off flush still applies
  on the audio-thread side (1.D.2 invariant preserved).
- [ ] **AC-1.D.3-B** `addPlugin` appends to the chain on the message
  thread after worker-thread instantiation; existing plugin instances
  retain all parameter state.
- [ ] **AC-1.D.3-C** `removeNode(id)` removes the node from the chain;
  surviving plugins retain state.
- [ ] **AC-1.D.3-D** `moveNode(id, newIndex)` reorders the chain;
  surviving plugins retain state and parameter values.
- [ ] **AC-1.D.3-E** `setNodeBypass(id, true)` ramps the BypassableInstance
  to bypass; the patch's `bypass` flag mirrors.
- [ ] **AC-1.D.3-F** `clearPatch()` empties the chain and the runtime
  routes audio input straight to output (passthrough).
- [ ] **AC-1.D.3-G** Restore-on-launch reloads the multi-plugin chain via
  `Preferences::getActivePatch`.
- [ ] **AC-1.D.3-H** `queuePatch` coalescing: rapid calls collapse to
  the last (1.D.2 latest-wins behaviour, generalised). Pending `addPlugin`
  requests are dropped if a `queuePatch` arrives.
- [ ] **AC-1.D.3-I** `queuePatch` with a partially-bogus patch
  (one node fails to instantiate) leaves the previous patch live;
  errorOut names the offending node.
- [ ] **AC-1.D.3-J** Each `addPlugin` is independent — 5 rapid calls
  produce 5 nodes in FIFO order; the addQueue is bounded at 16.
- [ ] **AC-1.D.3-K** Multiple EditorWindows open simultaneously — each
  keyed by NodeId; closing one doesn't affect others.
- [ ] **AC-1.D.3-L** `KnownPluginList` cross-check on every node in a
  restored patch; whole-patch refused if any node missing
  (1.D.2-L generalised).
- [ ] **AC-1.D.3-M** Crash-loop guard: marker file content is the
  patch's identity hash; matching hash on launch → skip + clear
  (1.D.2-N generalised).
- [ ] **AC-1.D.3-N** Migration from 1.D.2 single-plugin Preferences:
  one-shot read of `activePluginDescription`, conversion to a 1-node
  patch in `activePatch`, removal of the old key.
- [ ] **AC-1.D.3-O** Linear-chain connections are recomputed
  deterministically: in → node[0] → node[1] → ... → node[N-1] → out
  (per stereo channel).

### Non-Functional Requirements

- [ ] **AC-1.D.3-P** UI stays responsive during `addPlugin`
  instantiation (≥ 1 s plugin load); Add button disabled, status label
  shows "Loading <name>...", message-thread JUCE event loop continues
  to pump.
- [ ] **AC-1.D.3-Q** No allocations on the audio thread introduced by
  this phase. (JUCE's incremental graph mutation is documented as
  message-thread-only; verify with code review.)
- [ ] **AC-1.D.3-R** Worker thread joins on Session destruction within
  ≤ 5 s (1.D.2 invariant preserved).
- [ ] **AC-1.D.3-S** Whole-patch ack-wait still bounded by 2 s with
  synchronous fallback on timeout (1.D.2-R preserved).

### Quality Gates

- [ ] All new unit tests pass on `test:linux:unit`.
- [ ] All existing 1.D.2 tests still pass after the migration.
- [ ] No new `clang-format-18` violations on lint job.
- [ ] No new `clang-tidy-18` warnings on lint job.
- [ ] `pb3_apply_strict_warnings` covers the new `.cpp` files.
- [ ] macOS build job stays green.
- [ ] PluginSlot is fully removed (`grep PluginSlot` returns nothing in
  src/ and tests/).

### New from deepening (T–Z)

- [ ] **AC-1.D.3-T** `replacePatch` closes all open `EditorWindow`s
  before the swap commits (use-after-free fix). Tested by Session_test
  scenario "open 3 editors, replacePatch, assert editor map empty".
- [ ] **AC-1.D.3-U** Synchronous ops (`removeNode`, `moveNode`,
  `setNodeBypass`, `setNodeCustomName`) bump `stateVersion_` while
  keeping `state_ == Idle`. Polling harnesses observe each mutation.
- [ ] **AC-1.D.3-V** Restore-on-launch deferred until audio device
  reports `sampleRate > 0`; if SR never arrives, restore is abandoned
  cleanly without clearing `activePatch`.
- [ ] **AC-1.D.3-W** PreferencesPersister writes `activePatch` after
  every patch-mutating op (success path) — including synchronous
  bypass / move / remove. Diff-against-last-written to skip no-op
  writes.
- [ ] **AC-1.D.3-X** Restore rejects (and logs + clears `activePatch`)
  on any of: duplicate `NodeId`, `<CONN>` referencing absent node,
  cyclic connections.
- [ ] **AC-1.D.3-Y** Adding a heavy synth (Diva, Omnisphere, Serum)
  may produce one audible glitch on the *transition block*; documented
  limitation. Pre-warm via worker-thread `processBlock(silence)`
  reduces but does not eliminate this. Not a regression vs Phase 1.D.2.
- [ ] **AC-1.D.3-Z** `addPlugin`'s message-thread commit re-reads
  current patch tail and current runtime SR/block under the message-
  thread lock; stale worker-side snapshots are never used to compute
  connections or play config. Tested by a fake builder that blocks
  mid-build while the test fires a `moveNode`; release builder; assert
  the new node lands at the *current* tail.

## Success Metrics

- **Multi-plugin chain audibly works** — manual macOS smoke: load 3
  plugins (compressor → reverb → limiter), audio flows through all
  three, parameter changes on each are heard.
- **State preservation across reorder** — set a unique parameter value
  on a middle plugin, move it; the value persists.
- **Restore-on-launch fidelity** — quit with 3-plugin chain; relaunch;
  same chain reappears with same parameter state where state-blob
  persistence is in scope (not yet — Phase 2 — so this is identity-only
  fidelity for now).
- **No glitches on add/remove** — adding a plugin to a 5-node chain
  while audio plays produces no audible click on surviving plugins.

## Dependencies & Prerequisites

- Phase 1.D.2 lands in `main` (post-merge of MR #11). ✅
- `engine::AudioGraphRuntime` (Phase 1.A + 1.D.2). ✅
- `engine::BypassableInstance` (Phase 1.A). ✅
- `engine::PluginHost` with `KnownPluginList` (Phase 1.C.3+). ✅
- `session::Preferences` with XML round-trip (Phase 1.C.2). ✅
- JUCE 8.0.4 — `juce::AudioProcessorGraph` mutation API,
  `juce::SHA256` for marker hashing.
- No new third-party deps.

## Risk Analysis & Mitigation

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| JUCE's incremental graph mutation has a race we don't see in tests | Low | High | Stress test: 100 rapid add/remove pairs while audio pumps; run under `-fsanitize=thread` if Phase-0 promised tsan preset is wired. Fallback: every mutation does a whole-graph rebuild (slower but obviously safe). |
| State preservation across `moveNode` fails for plugins that latch on connection topology | Low | Medium | Trace via real plugins on macOS smoke; document any plugin that requires re-prepareToPlay on connection change. Most plugins are connection-stable. |
| Linear UI frustrates users who want parallel processing | Medium | Low | Document explicitly: "Phase 3 brings the canvas." Linear is the minimum viable; don't reinvent canvas UX in 1.D.3. |
| `addQueue_` overflow under user spam | Low | Low | Bounded at 16 with a clear log message + `Session::lastError_` populated. Throws no exception. |
| Restore-on-launch with a 20-plugin patch takes 60 s | Medium | Low | UI shows progress (status label + node-count). Allow `clearPatch` to abort. Document. |
| ~~Migration from `activePluginDescription`~~ — DROPPED | — | — | No migration helper; first 1.D.3 launch starts blank. Release-note item. |
| 1.D.2 → 1.D.3 schema drift: distinct root tags (`<ACTIVE_PATCH>` vs future `<PATCH schemaMajor="3">`) | Low | Low | Different root tags + no schema attribute on the 1.D.3 stub avoid the namespace collision the original draft introduced. |
| Heavy synth `addNode` blocks the message thread for `prepareToPlay` (~hundreds of ms to seconds for Diva, Omnisphere, Kontakt) | Medium | Medium | UI shows "Loading <name>..." status before the worker dispatches; worker pre-warms the new node via `processBlock(silence)` to reduce first-block cost; documented under AC-1.D.3-Y. |
| Audible glitch on the *transition block* when a heavy synth is added to a live chain at <512-frame buffer | Medium | Low | Documented limitation (AC-1.D.3-Y). Pre-warm reduces but does not eliminate. Recommend buffer ≥ 512 for hosts with heavy synths. |
| User downgrades from 1.D.3 back to 1.D.2 binary; `activePatch` key is unknown to 1.D.2 → silent loss of restore | Low | Low | Acceptable for v3.0-pre (no users yet). Document in release notes if downgrade is ever a supported path. |
| `EditorWindow` use-after-free on `replacePatch` swap (raw `AudioPluginInstance*` becomes dangling) | High (was) → Mitigated | High | AC-1.D.3-T: MainWindow listens for Session change-message; on transition into Building (replacePatch entry), close all open editors before the swap commits. |
| Multiple EditorWindows for separate plugin instances clash on resource (some plugins are non-reentrant) | Low | Medium | Out of scope to audit per-plugin; document as known plugin-quality concern. Most modern plugins handle multiple instances per host. |

## Resource Requirements

- 1 engineer × ~3 days for Units 1-9 with parallel subagent dispatch.
- macOS hardware (user's MaxBook) for end-to-end smoke.
- No CI-side changes — existing `pb3_session_core` static lib +
  `pb3_apply_strict_warnings` per-source already cover new files.

## Future Considerations

- **Phase 2 — `PdlSerializer` + state blobs.** `Patch` evolves to carry
  `juce::MemoryBlock state` per node; PdlSerializer writes/reads
  `<STATE encoding="base64">...</STATE>`. PatchRegistry layers on top.
- **Phase 3 — drag-to-connect canvas.** `MainWindow`'s linear chain
  panel is replaced by `PluginFieldView` with node cards, Bezier
  connections, lasso-select, keyboard nav. Engine layer unchanged
  (already supports arbitrary topology).
- **Phase 4 — MIDI/OSC mappings.** Mappings reference NodeIds within
  the active patch. The NodeId stability guarantee within a session
  is enough; cross-session mapping persistence is a Phase-4 concern.
- **State preservation across whole-patch swap (queuePatch).** If
  Phase 2 wants this, the cleanest path is `juce::AudioProcessorGraph::Node`
  reparenting via reference-counted `Node::Ptr`.
- **Per-node CPU meters.** `BypassableInstance::consumeCpuStats` already
  exists (Phase 1.A); MainWindow can poll per row in Phase 3 polish.
- **Branching topology UI.** Phase 3 canvas. Engine is ready.

## Documentation Plan

- `app-v3/src/session/Patch.h` — header doc-block describing the data
  model + diff semantics, ≤ 30 lines.
- `app-v3/src/session/Session.h` — update header doc-block for the
  multi-node API.
- `app-v3/src/session/PreparedGraph.h` — note the multi-node shape.
- `docs/architecture.md` (when written, master plan §10) — quote
  the Patch + Session shape from the headers.
- No user-facing docs in 1.D.3 — the visible feature ("you can now
  load multiple plugins") is self-documenting via the new chain UI.

## Resolve Before Planning

None. The master plan §4.2 + §4.5 spec the data model; the 1.D.2
implementation provides the state-machine foundation; the user's pain
point ("can only have one VST, can't connect plugins") aligns with the
phase's stated goal.

## Deferred to Implementation

- **NodeId scheme** — chosen `int` per master plan §4.5 schema; no
  decision needed at planning time.
- **Empty-patch routing** — chosen passthrough (audio in → audio out);
  documented above.
- **all-notes-off on individual node removal** — defer (held notes
  play out via plugin envelopes; rare in practice, and the worst
  outcome is a brief tail). Add as a Phase-1.D.3-followup if QA flags
  it.
- **`UpdateKind` arg on `removeNode` / `addNode`** — let JUCE default
  apply; verify with stress test in Unit 9.
- **Bound on `addQueue_`** — chosen 16; revisit only if QA hits the
  limit.
- **`SHA256` hash format for the crash-loop marker** — base64 (matches
  PluginDescription state-blob convention) vs hex string; pick at
  implementation. Doesn't affect correctness.
- **Worker-thread ordering: should `queuePatch` interrupt an
  in-progress `addPlugin` build?** The plan's coalescing rule says yes;
  open question whether to actually preempt the in-flight `createInstance`
  or just discard its result. Pragmatic: discard. preempting JUCE's
  plugin instantiation isn't safe.

## Scope Boundaries (Non-Goals)

- ❌ `.pdl` file save/load (Phase 2).
- ❌ Plugin state blob save/restore (Phase 2 PdlSerializer).
- ❌ Drag-to-connect UI canvas (Phase 3).
- ❌ Branching topology UI (Phase 3).
- ❌ MIDI / OSC mappings to nodes (Phase 4).
- ❌ Per-plugin CPU meter UI (Phase 3 polish).
- ❌ Patch presets / quick switch (Phase 2).
- ❌ Undo/redo for chain mutations (Phase 3).
- ❌ Multi-channel / surround topologies (master plan §4 invariant 2;
  Phase 1.D.3 stays stereo-only).
- ❌ State preservation across whole-patch swap (`queuePatch`) — only
  preserved across incremental ops in this phase.
- ❌ MIDI auto-routing — graphs include `midiInputNode` connected to
  every plugin's MIDI pin (linear chain default); Phase 3 lets users
  rewire.
- ❌ Plugin auto-rescan on launch (no change from 1.D.2).
- ❌ Latency compensation across the chain (Phase 5
  `LatencyCompensator`).

## Sources & References

### Origin

- **Origin document:**
  [docs/plans/2026-04-12-001-feat-pedalboard-3-rewrite-plan.md](2026-04-12-001-feat-pedalboard-3-rewrite-plan.md)
  — key decisions carried forward:
  - §4.2 places `Patch` + `PatchRegistry` on `session::Session`; this
    plan introduces the `Patch` half and the `Session` shape, deferring
    `PatchRegistry` to Phase 2.
  - §4.5 patch serialization schema (`<NODE id="N">` + `<CONNECTION>`)
    drives the `activePatch` Preferences XML form.
  - §4.1.1 patch-switch state machine — preserved from 1.D.2; extended
    to operate on whole-patch swaps where each "patch" carries N
    plugins instead of one.
  - Appendix A.1 PatchBuilder skeleton → `buildPreparedGraph` in
    multi-node form.

### Internal References

- `app-v3/src/session/Session.h` (current) — 1.D.2 single-slot API; the
  refactor target for Unit 4.
- `app-v3/src/session/PluginSlot.h/.cpp` — slated for deletion in Unit 5.
- `app-v3/src/session/PreparedGraph.h/.cpp` — slated for multi-node
  rewrite in Unit 2.
- `app-v3/src/session/Preferences.h:51-63` — 1.D.2 active-plugin-description
  pattern; extended to `activePatch` in Unit 3.
- `app-v3/src/engine/AudioGraphRuntime.cpp` — unchanged; 1.D.2
  pending_/live_ swap mechanism handles the whole-patch swap path.
- `app-v3/src/ui/MainWindow.cpp:62-401` — Phase-1 diagnostic chain row;
  rewritten in Unit 8.
- `app-v3/src/AppContext.h` — drops the `pluginSlot` field in Unit 5.

### External References

- JUCE 8.0.4 `juce::AudioProcessorGraph::Node`,
  `juce::AudioProcessorGraph::addNode/removeNode/addConnection`.
- JUCE 8.0.4 `juce::AudioProcessorGraph::midiChannelIndex` for MIDI
  routing within the graph.
- JUCE 8.0.4 `juce::SHA256` for the crash-loop marker hash.
- JUCE plugin host sample (`extras/AudioPluginHost`) — reference for
  `juce::AudioProcessorGraph::AudioGraphIOProcessor` integration with
  audio + midi I/O nodes (the existing `buildPreparedGraph` in 1.D.2
  cribs the pattern; Unit 2 generalises).

### Related Work

- Phase 1.D.2 plan:
  [docs/plans/2026-04-26-004-feat-patch-switch-state-machine-plan.md](2026-04-26-004-feat-patch-switch-state-machine-plan.md)
  — direct predecessor; provides the state machine and the
  audio-thread-side flush.
- Phase 1.C.2 plan:
  [docs/plans/2026-04-26-002-feat-preferences-persistence-plan.md](2026-04-26-002-feat-preferences-persistence-plan.md)
  — provides the Preferences XML round-trip foundation Unit 3
  extends.
- Phase 1.D.1 plan:
  [docs/plans/2026-04-26-003-feat-load-single-plugin-plan.md](2026-04-26-003-feat-load-single-plugin-plan.md)
  — the slot-and-graph foundation; Unit 5 retires the slot half of
  this work.
