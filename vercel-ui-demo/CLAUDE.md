# PedalBoard2 - AI Development Guide

## Quick Start

This is a **Next.js 16 + React 19** UI mockup for PedalBoard2, a VST/AU plugin host for live performance. The UI is complete but all audio functionality is mocked - this needs to connect to a real audio backend (likely Rust/C++ via Tauri or Electron).

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            app/page.tsx                                      │
│  Main layout orchestrator - manages panel visibility state                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                         TransportBar (top)                                   │
│  Audio settings, meters, play/stop, MIDI/OSC indicators                      │
├───────────────┬─────────────────────────────────┬───────────────────────────┤
│ PatchesPanel  │      SignalFlowCanvas           │     PluginBrowser         │
│ (left sidebar)│      (center - main area)       │     (right sidebar)       │
│               │                                 │                           │
│ - Patch list  │ - Draggable PluginNode tiles    │ - Available plugins       │
│ - Quick switch│ - SVG patch cable connections   │ - Search/filter           │
│ - Active/queue│ - Zoom controls                 │ - Drag to add (TODO)      │
├───────────────┴─────────────────────────────────┴───────────────────────────┤
│                        MidiMappingPanel (bottom)                             │
│  MIDI CC and OSC parameter mapping with Learn mode                           │
└─────────────────────────────────────────────────────────────────────────────┘
```

## File Structure

```
/app
  page.tsx              # Main layout, panel state management
  layout.tsx            # Root layout with metadata
  globals.css           # Tailwind v4 theme tokens (dark audio software aesthetic)

/components
  signal-flow-canvas.tsx   # Core: plugin graph + cable connections (470 lines)
  plugin-node.tsx          # Individual plugin tile with ports
  patches-panel.tsx        # Preset/patch management sidebar
  plugin-browser.tsx       # Available plugins list
  midi-mapping-panel.tsx   # MIDI/OSC parameter mapping
  transport-bar.tsx        # Top bar: audio settings, meters, transport
  audio-settings-dialog.tsx # Audio device configuration modal

/lib
  pedalboard-types.ts      # All TypeScript interfaces + documentation
  utils.ts                 # cn() helper (Tailwind class merge)
```

## Key Data Types (from lib/pedalboard-types.ts)

```typescript
// Plugin in the signal chain
interface Plugin {
  id: string
  name: string
  type: "effect" | "instrument" | "utility"
  x: number              // Canvas position
  y: number
  bypassed: boolean
  inputs: Port[]         // Audio input ports
  outputs: Port[]        // Audio output ports
  midiEnabled: boolean
}

// Audio connection port
interface Port {
  id: string
  type: "input" | "output"
  channel: "L" | "R" | "M"  // Left, Right, or Mono
  signalType: "audio" | "midi" | "cv"
}

// Cable between two plugins
interface Connection {
  id: string
  fromPluginId: string
  fromPort: Port
  toPluginId: string
  toPort: Port
}

// Saved preset (entire plugin chain + settings)
interface Patch {
  id: string
  name: string
  plugins: Plugin[]
  connections: Connection[]
  midiMappings: MidiMapping[]
  oscMappings: OscMapping[]
}
```

## Mock Data Locations

All mock data is clearly marked with `// === MOCK DATA ===` comments:

| File | Mock Data | Real Implementation |
|------|-----------|---------------------|
| `signal-flow-canvas.tsx` | `initialPlugins`, `initialConnections` | Load from patch file, query loaded VSTs |
| `patches-panel.tsx` | `MOCK_PATCHES` array | Load from filesystem, database |
| `plugin-browser.tsx` | `MOCK_PLUGINS` array | Scan VST directories, query plugin database |
| `midi-mapping-panel.tsx` | `MOCK_MIDI_MAPPINGS`, `MOCK_OSC_MAPPINGS` | Store in patch, sync with audio engine |
| `audio-settings-dialog.tsx` | `MOCK_AUDIO_DRIVERS`, device lists | Query OS audio APIs (WASAPI, CoreAudio, ASIO) |
| `transport-bar.tsx` | CPU/latency values | Poll from audio engine stats |

## Connection System (Key Logic)

The patch cable system in `signal-flow-canvas.tsx`:

1. **Port click** → `handlePortMouseDown` sets `activePort`
2. **Drag over port** → `handlePortMouseEnter` sets `hoveredPort`
3. **Mouse release** → `handleMouseUp` creates connection if:
   - Different plugins
   - Compatible types (output→input)
   - Not already connected
4. **Rendering** → SVG `<path>` with bezier curves between port positions

Port positions calculated in `getPortPosition()`:
- Input ports: left edge of plugin node
- Output ports: right edge of plugin node
- Y offset based on channel (L=0, R=20px spacing)

## Integration Points for Real Backend

### 1. Audio Engine Connection
```typescript
// Replace mock state with real audio engine queries
interface AudioEngine {
  loadPlugin(path: string): Promise<Plugin>
  unloadPlugin(id: string): void
  connect(fromId: string, fromPort: string, toId: string, toPort: string): void
  disconnect(connectionId: string): void
  setParameter(pluginId: string, paramId: string, value: number): void
  getStats(): { cpu: number, latency: number }
}
```

### 2. Plugin Scanning
```typescript
// Replace MOCK_PLUGINS with real scanner
interface PluginScanner {
  scanDirectory(path: string): Promise<PluginInfo[]>
  getPluginInfo(path: string): PluginInfo
  validatePlugin(path: string): boolean
}
```

### 3. MIDI/OSC
```typescript
// Replace mock mappings with real I/O
interface MidiOscBridge {
  onMidiCC(callback: (channel: number, cc: number, value: number) => void): void
  onOscMessage(callback: (address: string, value: number) => void): void
  sendOsc(address: string, value: number): void
}
```

### 4. Patch Persistence
```typescript
// Replace mock patches with file I/O
interface PatchManager {
  save(patch: Patch, path: string): Promise<void>
  load(path: string): Promise<Patch>
  listPatches(directory: string): Promise<string[]>
}
```

## UI Constants (for layout consistency)

```typescript
// Plugin node dimensions
const NODE_WIDTH = 170      // px
const NODE_HEIGHT = 140     // px (approx, varies with ports)
const PORT_Y_BASE = 75      // px from top of node
const PORT_SPACING = 20     // px between L/R ports

// Canvas
const GRID_SIZE = 20        // px snap grid
const MIN_ZOOM = 50         // %
const MAX_ZOOM = 150        // %

// Colors (from globals.css)
// Primary: oklch(0.72 0.19 160) - teal/cyan accent
// Background: oklch(0.12 0.01 250) - dark blue-gray
// Cards: oklch(0.16 0.01 250) - slightly lighter
```

## Styling

- **Tailwind CSS v4** with custom theme tokens in `globals.css`
- **shadcn/ui** components in `/components/ui/`
- **Dark theme only** - designed for stage visibility
- **Color palette**: Dark blue-grays + teal accent

## Common Tasks

### Add a new plugin type
1. Update `PluginType` in `pedalboard-types.ts`
2. Add icon mapping in `plugin-browser.tsx` `typeIcons`
3. Add color variant if needed in `plugin-node.tsx`

### Add a new port signal type
1. Update `SignalType` in `pedalboard-types.ts`
2. Update connection validation in `signal-flow-canvas.tsx`
3. Update port color styling in `plugin-node.tsx`

### Add new audio settings
1. Add to `AudioSettings` interface in `pedalboard-types.ts`
2. Add UI controls in `audio-settings-dialog.tsx`
3. Display in `transport-bar.tsx` if needed

### Connect to real audio backend
1. Create adapter implementing interfaces above
2. Replace `useState` with backend queries (use SWR for caching)
3. Replace mock arrays with backend calls
4. Add error handling and loading states

## Testing Checklist

- [ ] Plugin nodes drag smoothly (no cable lag)
- [ ] Cables connect output→input only
- [ ] Cables disconnect on port click
- [ ] Patches switch instantly
- [ ] Audio settings save correctly
- [ ] MIDI learn mode captures input
- [ ] Zoom controls work
- [ ] Keyboard shortcuts (Delete for cables, Escape to cancel)
