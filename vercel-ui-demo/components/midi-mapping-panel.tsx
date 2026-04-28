/**
 * =============================================================================
 * MIDI/OSC MAPPING PANEL
 * =============================================================================
 * 
 * Panel for managing parameter mappings from external controllers.
 * Supports both MIDI CC messages and OSC (Open Sound Control) messages.
 * 
 * MIDI MAPPINGS:
 * --------------
 * - Source: MIDI Channel + CC Number (e.g., Ch 1, CC 1)
 * - Target: Plugin parameter (e.g., "Shimmer / Mix")
 * - Value: Current normalized value (0-100%)
 * 
 * Common MIDI CC assignments:
 * - CC 1: Modulation wheel
 * - CC 7: Volume
 * - CC 11: Expression
 * - CC 64: Sustain pedal
 * - CC 74: Filter cutoff (common in synths)
 * 
 * OSC MAPPINGS:
 * -------------
 * - Source: OSC address path (e.g., "/filter/cutoff")
 * - OSC messages arrive on configurable port (default: 8000)
 * - Useful for TouchOSC, Lemur, custom controllers
 * 
 * LEARN MODE:
 * -----------
 * When "Learn" is active:
 * 1. User clicks a plugin parameter in the canvas (not implemented)
 * 2. User moves a physical controller
 * 3. System captures the MIDI/OSC message and creates mapping
 * 
 * INTEGRATION POINTS:
 * -------------------
 * In a real implementation:
 * 1. Mappings would be stored per-patch
 * 2. MIDI input would come from Web MIDI API or native backend
 * 3. OSC would use a WebSocket bridge to native OSC server
 * 4. Parameter targets would reference actual plugin parameter IDs
 */

"use client"

import { useState } from "react"
import { Keyboard, Radio, Plus, Trash2, Link2 } from "lucide-react"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

interface MidiMapping {
  id: string
  source: string        // Display name for the source
  target: string        // Target parameter path
  type: "midi" | "osc"
  channel?: number      // MIDI channel (1-16)
  cc?: number           // MIDI CC number (0-127)
  oscPath?: string      // OSC address path
  value: number         // Current value (0-100)
}

// =============================================================================
// MOCK DATA - Replace with real mappings from patch state
// =============================================================================

/**
 * MOCK: Example parameter mappings.
 * In a real app, mappings would:
 * - Be stored as part of each patch
 * - Reference actual plugin instance + parameter IDs
 * - Update in real-time as MIDI/OSC messages arrive
 * - Support min/max range scaling and curves
 */
const initialMappings: MidiMapping[] = [
  { id: "1", source: "CC 1", target: "Shimmer / Mix", type: "midi", channel: 1, cc: 1, value: 75 },
  { id: "2", source: "CC 2", target: "Pro-Q 3 / Band 1 Gain", type: "midi", channel: 1, cc: 2, value: 50 },
  { id: "3", source: "/filter/cutoff", target: "Decapitator / Tone", type: "osc", oscPath: "/filter/cutoff", value: 30 },
  { id: "4", source: "CC 7", target: "Master Volume", type: "midi", channel: 1, cc: 7, value: 100 },
]

// =============================================================================
// COMPONENT
// =============================================================================

export function MidiMappingPanel() {
  const [mappings, setMappings] = useState<MidiMapping[]>(initialMappings)
  const [activeTab, setActiveTab] = useState<"midi" | "osc">("midi")
  const [isLearning, setIsLearning] = useState(false)

  const filteredMappings = mappings.filter((m) => m.type === activeTab)

  return (
    <div className="flex h-full flex-col border-t border-border bg-card">
      {/* Header with tabs */}
      <div className="flex items-center justify-between border-b border-border px-4 py-2">
        {/* Tab buttons */}
        <div className="flex items-center gap-1">
          <Button
            variant={activeTab === "midi" ? "secondary" : "ghost"}
            size="sm"
            className="h-7 gap-1.5 text-xs"
            onClick={() => setActiveTab("midi")}
          >
            <Keyboard className="h-3.5 w-3.5" />
            MIDI
          </Button>
          <Button
            variant={activeTab === "osc" ? "secondary" : "ghost"}
            size="sm"
            className="h-7 gap-1.5 text-xs"
            onClick={() => setActiveTab("osc")}
          >
            <Radio className="h-3.5 w-3.5" />
            OSC
          </Button>
        </div>

        {/* Actions */}
        <div className="flex items-center gap-2">
          {/* Learn mode toggle */}
          <Button
            variant={isLearning ? "default" : "outline"}
            size="sm"
            className={cn("h-7 text-xs", isLearning && "animate-pulse")}
            onClick={() => setIsLearning(!isLearning)}
          >
            <Link2 className="mr-1.5 h-3.5 w-3.5" />
            {isLearning ? "Learning..." : "Learn"}
          </Button>
          <Button variant="ghost" size="icon" className="h-7 w-7" aria-label="Add mapping">
            <Plus className="h-4 w-4" />
          </Button>
        </div>
      </div>

      {/* Mappings table */}
      <div className="flex-1 overflow-auto">
        <table className="w-full text-sm">
          <thead>
            <tr className="border-b border-border text-muted-foreground">
              <th className="px-4 py-2 text-left font-medium">Source</th>
              <th className="px-4 py-2 text-left font-medium">Target</th>
              <th className="px-4 py-2 text-right font-medium">Value</th>
              <th className="w-10"></th>
            </tr>
          </thead>
          <tbody>
            {filteredMappings.map((mapping) => (
              <tr
                key={mapping.id}
                className="group border-b border-border/50 hover:bg-muted/30"
              >
                {/* Source column */}
                <td className="px-4 py-2">
                  <div className="flex items-center gap-2">
                    {mapping.type === "midi" ? (
                      <Keyboard className="h-3.5 w-3.5 text-primary" />
                    ) : (
                      <Radio className="h-3.5 w-3.5 text-chart-3" />
                    )}
                    <span className="font-mono text-xs">{mapping.source}</span>
                  </div>
                </td>
                {/* Target column */}
                <td className="px-4 py-2">
                  <span className="text-muted-foreground">{mapping.target}</span>
                </td>
                {/* Value column with progress bar */}
                <td className="px-4 py-2 text-right">
                  <div className="flex items-center justify-end gap-2">
                    <div className="h-1.5 w-16 overflow-hidden rounded-full bg-muted">
                      <div
                        className="h-full rounded-full bg-primary"
                        style={{ width: `${mapping.value}%` }}
                      />
                    </div>
                    <span className="w-8 text-right font-mono text-xs text-muted-foreground">
                      {mapping.value}%
                    </span>
                  </div>
                </td>
                {/* Delete button */}
                <td className="px-2 py-2">
                  <Button
                    variant="ghost"
                    size="icon"
                    className="h-6 w-6 opacity-0 group-hover:opacity-100"
                    aria-label="Delete mapping"
                  >
                    <Trash2 className="h-3 w-3 text-muted-foreground hover:text-destructive" />
                  </Button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>

        {/* Empty state */}
        {filteredMappings.length === 0 && (
          <div className="flex flex-col items-center justify-center py-8 text-muted-foreground">
            <p className="text-sm">No {activeTab.toUpperCase()} mappings</p>
            <p className="text-xs">Click Learn to create a mapping</p>
          </div>
        )}
      </div>

      {/* Footer status */}
      <div className="flex items-center justify-between border-t border-border px-4 py-2 text-xs text-muted-foreground">
        <span>{filteredMappings.length} mappings</span>
        {/* MOCK: OSC port indicator */}
        {activeTab === "osc" && <span>Listening on port 8000</span>}
      </div>
    </div>
  )
}
