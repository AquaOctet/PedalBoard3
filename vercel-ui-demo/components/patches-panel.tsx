/**
 * =============================================================================
 * PATCHES PANEL
 * =============================================================================
 * 
 * Sidebar panel for managing saved patches (presets).
 * 
 * LIVE PERFORMANCE FEATURES:
 * --------------------------
 * - Active patch: Currently loaded and playing
 * - Queued patch: Will switch to this patch on next trigger
 * 
 * The queue system allows performers to prepare the next patch while the
 * current one is still playing. Switching can be triggered by:
 * - Double-clicking a patch
 * - MIDI program change (not implemented in UI mock)
 * - Footswitch / external controller (not implemented in UI mock)
 * 
 * INTERACTIONS:
 * -------------
 * - Single click: Select patch (shows details, but doesn't load)
 * - Double click: Activate patch (loads and plays immediately)
 * - Click play icon: Toggle queue status
 * - Hover: Shows copy/delete actions
 * 
 * INTEGRATION POINTS:
 * -------------------
 * In a real implementation:
 * 1. Patches would be loaded from a patches directory or database
 * 2. Activating a patch would send the full state to SignalFlowCanvas
 * 3. MIDI program change messages would trigger patch switches
 * 4. Patch files would store: plugins, connections, mappings, settings
 */

"use client"

import { useState } from "react"
import { Play, Plus, Copy, Trash2, ChevronRight } from "lucide-react"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

interface Patch {
  id: string
  name: string
  isActive: boolean
  isQueued: boolean
}

// =============================================================================
// MOCK DATA - Replace with real patch loading from filesystem/database
// =============================================================================

/**
 * MOCK: Example patches for demonstration.
 * In a real app, patches would be loaded from:
 * - User's patch directory (e.g., ~/Documents/PedalBoard2/Patches/)
 * - Cloud storage sync
 * - Import from file
 * 
 * Each patch file would contain the full serialized state:
 * - Plugin instances and positions
 * - Cable connections
 * - MIDI/OSC mappings
 * - Audio settings overrides
 */
const initialPatches: Patch[] = [
  { id: "1", name: "Clean Ambient", isActive: true, isQueued: false },
  { id: "2", name: "Heavy Distortion", isActive: false, isQueued: true },
  { id: "3", name: "Delay Shimmer", isActive: false, isQueued: false },
  { id: "4", name: "Bass Synth Pad", isActive: false, isQueued: false },
  { id: "5", name: "Vocal FX Chain", isActive: false, isQueued: false },
]

// =============================================================================
// COMPONENT
// =============================================================================

export function PatchesPanel() {
  const [patches, setPatches] = useState<Patch[]>(initialPatches)
  const [selectedId, setSelectedId] = useState<string>("1")

  /**
   * Activate a patch - loads it immediately.
   * In a real app, this would:
   * 1. Save current patch state if modified
   * 2. Load new patch's plugins, connections, mappings
   * 3. Crossfade or instant-switch audio based on settings
   */
  const handleActivate = (id: string) => {
    setPatches(
      patches.map((p) => ({
        ...p,
        isActive: p.id === id,
        isQueued: p.id === id ? false : p.isQueued,
      }))
    )
    setSelectedId(id)
  }

  /**
   * Toggle queue status for a patch.
   * Queued patches show a "next" indicator and will be loaded on trigger.
   */
  const handleQueue = (id: string) => {
    setPatches(
      patches.map((p) =>
        p.id === id ? { ...p, isQueued: !p.isQueued } : p
      )
    )
  }

  return (
    <div className="flex h-full flex-col bg-sidebar">
      {/* Header */}
      <div className="flex items-center justify-between border-b border-sidebar-border px-4 py-3">
        <h2 className="text-sm font-semibold text-sidebar-foreground">Patches</h2>
        <Button variant="ghost" size="icon" className="h-7 w-7" aria-label="Add new patch">
          <Plus className="h-4 w-4" />
        </Button>
      </div>

      {/* Patch list */}
      <div className="flex-1 overflow-auto p-2">
        <div className="flex flex-col gap-1">
          {patches.map((patch) => (
            <div
              key={patch.id}
              className={cn(
                "group flex items-center gap-2 rounded-md px-3 py-2 transition-colors cursor-pointer",
                patch.isActive
                  ? "bg-primary text-primary-foreground"
                  : selectedId === patch.id
                  ? "bg-sidebar-accent text-sidebar-accent-foreground"
                  : "hover:bg-sidebar-accent/50"
              )}
              onClick={() => setSelectedId(patch.id)}
              onDoubleClick={() => handleActivate(patch.id)}
            >
              {/* Status icon - play/queue indicator */}
              <button
                className={cn(
                  "flex h-5 w-5 items-center justify-center rounded",
                  patch.isActive
                    ? "bg-primary-foreground/20"
                    : patch.isQueued
                    ? "bg-chart-3/20"
                    : "bg-transparent"
                )}
                onClick={(e) => {
                  e.stopPropagation()
                  handleQueue(patch.id)
                }}
                aria-label={patch.isActive ? "Currently playing" : patch.isQueued ? "Queued" : "Click to queue"}
              >
                {patch.isActive ? (
                  <Play className="h-3 w-3 fill-current" />
                ) : patch.isQueued ? (
                  <ChevronRight className="h-3 w-3 text-chart-3" />
                ) : null}
              </button>

              {/* Patch name */}
              <span className="flex-1 truncate text-sm">{patch.name}</span>

              {/* Hover actions */}
              <div className="flex items-center gap-1 opacity-0 group-hover:opacity-100">
                <Button
                  variant="ghost"
                  size="icon"
                  className={cn(
                    "h-6 w-6",
                    patch.isActive
                      ? "hover:bg-primary-foreground/20 text-primary-foreground"
                      : "hover:bg-sidebar-accent"
                  )}
                  onClick={(e) => e.stopPropagation()}
                  aria-label="Duplicate patch"
                >
                  <Copy className="h-3 w-3" />
                </Button>
                <Button
                  variant="ghost"
                  size="icon"
                  className={cn(
                    "h-6 w-6",
                    patch.isActive
                      ? "hover:bg-primary-foreground/20 text-primary-foreground"
                      : "hover:bg-sidebar-accent"
                  )}
                  onClick={(e) => e.stopPropagation()}
                  aria-label="Delete patch"
                >
                  <Trash2 className="h-3 w-3" />
                </Button>
              </div>
            </div>
          ))}
        </div>
      </div>

      {/* Footer stats */}
      <div className="border-t border-sidebar-border p-3">
        <div className="flex items-center justify-between text-xs text-muted-foreground">
          <span>{patches.length} patches</span>
          <span>{patches.filter(p => p.isQueued).length} queued</span>
        </div>
      </div>
    </div>
  )
}
