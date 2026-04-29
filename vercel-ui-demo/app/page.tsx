/**
 * =============================================================================
 * PEDALBOARD2 - MAIN APPLICATION PAGE
 * =============================================================================
 * 
 * This is the root page component that assembles the PedalBoard2 UI.
 * 
 * LAYOUT STRUCTURE:
 * -----------------
 * The app uses a flex column layout filling the viewport height:
 * 
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │ TransportBar (h-12)                                                     │
 *   │ - App logo, audio device info, CPU/latency meters, MIDI/OSC status     │
 *   │ - Audio settings dialog accessible via gear icon                        │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *   ┌──────────────┬─────────────────────────────────────────┬───────────────┐
 *   │ PatchesPanel │ SignalFlowCanvas (flex-1)               │ PluginBrowser │
 *   │ (w-64)       │ - Zoomable canvas with grid background  │ (w-64)        │
 *   │              │ - Draggable PluginNode components       │               │
 *   │ - Patch list │ - SVG cables connecting ports           │ - Search      │
 *   │ - Active/    │ - Click ports to connect/disconnect     │ - Filter      │
 *   │   queued     ├─────────────────────────────────────────┤ - Drag to add │
 *   │   indicators │ MidiMappingPanel (h-48)                 │               │
 *   │              │ - MIDI CC mappings tab                  │               │
 *   │              │ - OSC mappings tab                      │               │
 *   │              │ - Learn mode for easy assignment        │               │
 *   └──────────────┴─────────────────────────────────────────┴───────────────┘
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │ StatusBar (h-8) - Current patch, running status, panel toggles         │
 *   └─────────────────────────────────────────────────────────────────────────┘
 * 
 * PANEL VISIBILITY:
 * -----------------
 * Three panels can be toggled via buttons in the status bar:
 * - showPatchesPanel: Left sidebar with patch presets
 * - showMappingPanel: Bottom panel with MIDI/OSC mappings
 * - showPluginBrowser: Right sidebar with available plugins
 * 
 * COMPONENT RESPONSIBILITIES:
 * ---------------------------
 * - TransportBar: Audio settings, system status, master controls
 * - PatchesPanel: Patch management (load, save, queue for live switching)
 * - SignalFlowCanvas: Visual signal routing with draggable nodes and cables
 * - MidiMappingPanel: External controller assignments
 * - PluginBrowser: Plugin discovery and addition to canvas
 * 
 * FUTURE INTEGRATION POINTS:
 * --------------------------
 * To connect this UI to a real audio backend:
 * 1. Replace useState with a global state manager (Zustand, Redux, etc.)
 * 2. Add WebSocket or IPC connection to native audio engine
 * 3. Sync plugin/connection state bidirectionally with backend
 * 4. Implement real audio device enumeration via backend API
 * 5. Add real MIDI/OSC message handling
 */

"use client"

import { useState } from "react"
import { PanelLeft, PanelRight, PanelBottom } from "lucide-react"
import { Button } from "@/components/ui/button"
import { TransportBar } from "@/components/transport-bar"
import { PatchesPanel } from "@/components/patches-panel"
import { SignalFlowCanvas } from "@/components/signal-flow-canvas"
import { MidiMappingPanel } from "@/components/midi-mapping-panel"
import { PluginBrowser } from "@/components/plugin-browser"
import { cn } from "@/lib/utils"

export default function PedalBoard2() {
  // Panel visibility state - controls which sidebars/panels are shown
  const [showPatchesPanel, setShowPatchesPanel] = useState(true)
  const [showPluginBrowser, setShowPluginBrowser] = useState(true)
  const [showMappingPanel, setShowMappingPanel] = useState(true)

  return (
    <div className="flex h-screen flex-col bg-background text-foreground">
      {/* Top transport bar - always visible */}
      <TransportBar />

      {/* Main content area - horizontal flex layout */}
      <div className="flex flex-1 overflow-hidden">
        {/* Left sidebar - Patches panel */}
        <div
          className={cn(
            "w-64 flex-shrink-0 transition-all duration-200",
            !showPatchesPanel && "w-0 overflow-hidden"
          )}
        >
          <PatchesPanel />
        </div>

        {/* Center - Signal flow canvas and mapping panel (vertical stack) */}
        <div className="flex flex-1 flex-col overflow-hidden">
          {/* Signal flow canvas - takes remaining vertical space */}
          <SignalFlowCanvas />

          {/* Bottom mapping panel - collapsible */}
          <div
            className={cn(
              "h-48 flex-shrink-0 transition-all duration-200",
              !showMappingPanel && "h-0 overflow-hidden"
            )}
          >
            <MidiMappingPanel />
          </div>
        </div>

        {/* Right sidebar - Plugin browser */}
        <div
          className={cn(
            "w-64 flex-shrink-0 transition-all duration-200",
            !showPluginBrowser && "w-0 overflow-hidden"
          )}
        >
          <PluginBrowser />
        </div>
      </div>

      {/* Bottom status bar */}
      <footer className="flex h-8 items-center justify-between border-t border-border bg-card px-4 text-xs text-muted-foreground">
        <div className="flex items-center gap-4">
          {/* MOCK: Current patch name - would come from active patch state */}
          <span>Patch: Clean Ambient</span>
          <span className="flex items-center gap-1">
            <span className="h-1.5 w-1.5 rounded-full bg-primary animate-pulse" />
            Running
          </span>
        </div>

        {/* Panel toggle buttons */}
        <div className="flex items-center gap-1">
          <Button
            variant="ghost"
            size="icon"
            className={cn("h-6 w-6", showPatchesPanel && "text-primary")}
            onClick={() => setShowPatchesPanel(!showPatchesPanel)}
            aria-label="Toggle patches panel"
          >
            <PanelLeft className="h-3.5 w-3.5" />
          </Button>
          <Button
            variant="ghost"
            size="icon"
            className={cn("h-6 w-6", showMappingPanel && "text-primary")}
            onClick={() => setShowMappingPanel(!showMappingPanel)}
            aria-label="Toggle mapping panel"
          >
            <PanelBottom className="h-3.5 w-3.5" />
          </Button>
          <Button
            variant="ghost"
            size="icon"
            className={cn("h-6 w-6", showPluginBrowser && "text-primary")}
            onClick={() => setShowPluginBrowser(!showPluginBrowser)}
            aria-label="Toggle plugin browser"
          >
            <PanelRight className="h-3.5 w-3.5" />
          </Button>
        </div>

        <div className="flex items-center gap-4">
          <span>v2.0.0</span>
        </div>
      </footer>
    </div>
  )
}
