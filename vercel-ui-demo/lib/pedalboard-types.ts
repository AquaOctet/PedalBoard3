/**
 * =============================================================================
 * PEDALBOARD2 TYPE DEFINITIONS
 * =============================================================================
 * 
 * This file contains all shared TypeScript interfaces for the PedalBoard2 app.
 * These types define the data structures used throughout the application.
 * 
 * ARCHITECTURE OVERVIEW:
 * ----------------------
 * PedalBoard2 is a VST/AU plugin host UI for live performance. The architecture:
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                          TransportBar                                   │
 *   │  (Audio settings, CPU meter, MIDI/OSC status, master controls)         │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *   ┌──────────────┬─────────────────────────────────────────┬───────────────┐
 *   │              │                                         │               │
 *   │  PatchesPanel│         SignalFlowCanvas                │ PluginBrowser │
 *   │  (Presets)   │   (Draggable plugin nodes + cables)     │ (Plugin list) │
 *   │              │                                         │               │
 *   │              ├─────────────────────────────────────────┤               │
 *   │              │         MidiMappingPanel                │               │
 *   │              │   (MIDI CC / OSC parameter mappings)    │               │
 *   └──────────────┴─────────────────────────────────────────┴───────────────┘
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                          StatusBar (Footer)                             │
 *   └─────────────────────────────────────────────────────────────────────────┘
 * 
 * DATA FLOW:
 * ----------
 * 1. Plugins are positioned on SignalFlowCanvas and can be dragged
 * 2. Connections (cables) link output ports to input ports between plugins
 * 3. Patches store complete configurations (plugins + connections + mappings)
 * 4. MIDI/OSC mappings link external controllers to plugin parameters
 * 5. Audio settings configure the audio driver and device routing
 */

// =============================================================================
// AUDIO SIGNAL FLOW TYPES
// =============================================================================

/**
 * Represents a connection port on a plugin node.
 * Each plugin has input and output ports for left (L) and right (R) channels.
 * 
 * Port ID format: "{pluginId}-{in|out}-{L|R}"
 * Example: "2-out-L" = Plugin 2's left output
 */
export interface Port {
  id: string
  type: "input" | "output"
  channel: "L" | "R"
}

/**
 * Represents a VST/AU plugin instance on the canvas.
 * 
 * Coordinate system:
 * - (0,0) is top-left of the canvas
 * - x increases to the right
 * - y increases downward
 * - Node width is fixed at 170px (see PluginNode component)
 */
export interface Plugin {
  id: string
  name: string
  type: "effect" | "instrument" | "utility"
  bypassed: boolean
  x: number  // Canvas X position in pixels
  y: number  // Canvas Y position in pixels
}

/**
 * Represents a cable connection between two plugin ports.
 * 
 * Connection rules:
 * - Output ports can only connect to input ports
 * - One output can connect to multiple inputs (signal splitting)
 * - One input can only receive from one output
 * - No same-plugin connections (can't connect a plugin to itself)
 */
export interface Connection {
  id: string
  fromPluginId: string
  fromPort: Port
  toPluginId: string
  toPort: Port
}

// =============================================================================
// PATCH / PRESET TYPES
// =============================================================================

/**
 * Represents a saved patch (preset) configuration.
 * A patch stores the complete state of the signal chain.
 * 
 * Live performance features:
 * - isActive: Currently playing patch
 * - isQueued: Will switch to this patch on next trigger (MIDI, footswitch)
 */
export interface Patch {
  id: string
  name: string
  isActive: boolean
  isQueued: boolean
  // NOTE: In a real implementation, these would also be included:
  // plugins: Plugin[]
  // connections: Connection[]
  // mappings: MidiMapping[]
}

// =============================================================================
// MIDI / OSC MAPPING TYPES
// =============================================================================

/**
 * Represents a parameter mapping from a MIDI CC or OSC message to a plugin parameter.
 * 
 * MIDI mappings use channel + CC number.
 * OSC mappings use an OSC address path (e.g., "/filter/cutoff").
 * 
 * The value (0-100) represents the current normalized value of the mapping.
 */
export interface MidiMapping {
  id: string
  source: string              // Display name (e.g., "CC 1" or "/filter/cutoff")
  target: string              // Target parameter (e.g., "Shimmer / Mix")
  type: "midi" | "osc"
  channel?: number            // MIDI channel (1-16), only for MIDI type
  cc?: number                 // MIDI CC number (0-127), only for MIDI type
  oscPath?: string            // OSC address path, only for OSC type
  value: number               // Current value (0-100)
}

// =============================================================================
// AUDIO SETTINGS TYPES
// =============================================================================

/**
 * Audio driver and device configuration.
 * 
 * Latency calculation:
 *   Round-trip latency = (bufferSize / sampleRate) * 1000 * 2 ms
 *   Example: 256 samples @ 48kHz = 10.67ms round-trip
 */
export interface AudioSettings {
  driver: string              // "asio" | "wasapi" | "directsound" | "coreaudio"
  inputDevice: string         // Device ID from DEVICES.input
  outputDevice: string        // Device ID from DEVICES.output
  sampleRate: string          // "44100" | "48000" | "96000" etc.
  bufferSize: string          // "64" | "128" | "256" | "512" etc.
  inputChannels: number[]     // Active input channels [1, 2, ...]
  outputChannels: number[]    // Active output channels [1, 2, ...]
  inputGain: number           // Input gain in dB (-24 to +24)
  outputGain: number          // Output gain in dB (-24 to +24)
  exclusiveMode: boolean      // Exclusive device access (ASIO)
}

// =============================================================================
// PLUGIN BROWSER TYPES
// =============================================================================

/**
 * Plugin metadata for the browser/search panel.
 * This represents available plugins that can be added to the canvas.
 */
export interface PluginInfo {
  id: string
  name: string
  vendor: string
  type: "effect" | "instrument" | "utility"
  category: string  // "Reverb", "EQ", "Compressor", "Synthesizer", etc.
}

// =============================================================================
// UI STATE TYPES
// =============================================================================

/**
 * Tracks which port is currently being dragged for cable connection.
 */
export interface ActivePort {
  pluginId: string
  port: Port
}

/**
 * Canvas viewport state for zoom/pan.
 */
export interface ViewportState {
  zoom: number      // Zoom percentage (50-150)
  panX: number      // Horizontal pan offset
  panY: number      // Vertical pan offset
}
