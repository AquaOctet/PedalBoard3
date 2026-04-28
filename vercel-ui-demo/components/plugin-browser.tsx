/**
 * =============================================================================
 * PLUGIN BROWSER
 * =============================================================================
 * 
 * Sidebar panel for discovering and adding plugins to the canvas.
 * 
 * PLUGIN TYPES:
 * -------------
 * - Effect: Audio processing (reverb, EQ, compression, etc.)
 * - Instrument: Sound generators (synths, samplers, drum machines)
 * - Utility: Signal routing, metering, analysis tools
 * 
 * CATEGORIES:
 * -----------
 * Effects are further categorized by their function:
 * - Reverb, Delay, EQ, Compressor, Saturation, Mastering, Lo-Fi, etc.
 * 
 * INTERACTIONS:
 * -------------
 * - Search: Filter by name, vendor, or category
 * - Type filter: Show only effects, instruments, or all
 * - Drag to canvas: Adds plugin instance at drop location (not yet implemented)
 * 
 * INTEGRATION POINTS:
 * -------------------
 * In a real implementation:
 * 1. Plugin list would come from scanning VST/AU directories
 * 2. Dragging to canvas would create a new plugin instance
 * 3. Plugin icons/thumbnails could be loaded from plugin metadata
 * 4. Favorites/recently used would be tracked per-user
 * 5. Plugin validation status (32/64-bit, compatible, etc.)
 */

"use client"

import { useState } from "react"
import { Search, Filter, Plug, Music, Sliders, Sparkles } from "lucide-react"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

interface PluginInfo {
  id: string
  name: string
  vendor: string
  type: "effect" | "instrument" | "utility"
  category: string
}

// =============================================================================
// MOCK DATA - Replace with real plugin scanning results
// =============================================================================

/**
 * MOCK: Available plugins list.
 * In a real app, this would be populated by:
 * - Scanning VST3/AU plugin directories
 * - Reading plugin metadata (name, vendor, category)
 * - Validating plugin compatibility
 * - Caching scan results for faster startup
 * 
 * Plugin formats typically supported:
 * - VST3 (Windows, macOS, Linux)
 * - AU/Audio Unit (macOS only)
 * - AAX (Pro Tools only)
 * - CLAP (newer cross-platform format)
 */
const availablePlugins: PluginInfo[] = [
  { id: "p1", name: "Valhalla Shimmer", vendor: "Valhalla DSP", type: "effect", category: "Reverb" },
  { id: "p2", name: "FabFilter Pro-Q 3", vendor: "FabFilter", type: "effect", category: "EQ" },
  { id: "p3", name: "Soundtoys Decapitator", vendor: "Soundtoys", type: "effect", category: "Saturation" },
  { id: "p4", name: "Serum", vendor: "Xfer Records", type: "instrument", category: "Synthesizer" },
  { id: "p5", name: "Ozone 11", vendor: "iZotope", type: "effect", category: "Mastering" },
  { id: "p6", name: "Kontakt 7", vendor: "Native Instruments", type: "instrument", category: "Sampler" },
  { id: "p7", name: "Pro-C 2", vendor: "FabFilter", type: "effect", category: "Compressor" },
  { id: "p8", name: "Replika XT", vendor: "Native Instruments", type: "effect", category: "Delay" },
  { id: "p9", name: "Vital", vendor: "Matt Tytel", type: "instrument", category: "Synthesizer" },
  { id: "p10", name: "RC-20 Retro Color", vendor: "XLN Audio", type: "effect", category: "Lo-Fi" },
]

// =============================================================================
// ICON/COLOR MAPS
// =============================================================================

const typeIcons = {
  effect: Sliders,
  instrument: Music,
  utility: Plug,
}

const typeColors = {
  effect: "text-primary",
  instrument: "text-chart-3",
  utility: "text-chart-2",
}

// =============================================================================
// COMPONENT
// =============================================================================

export function PluginBrowser() {
  const [searchQuery, setSearchQuery] = useState("")
  const [selectedType, setSelectedType] = useState<"all" | "effect" | "instrument" | "utility">("all")

  // Filter plugins based on search and type
  const filteredPlugins = availablePlugins.filter((plugin) => {
    const matchesSearch =
      plugin.name.toLowerCase().includes(searchQuery.toLowerCase()) ||
      plugin.vendor.toLowerCase().includes(searchQuery.toLowerCase()) ||
      plugin.category.toLowerCase().includes(searchQuery.toLowerCase())
    const matchesType = selectedType === "all" || plugin.type === selectedType
    return matchesSearch && matchesType
  })

  return (
    <div className="flex h-full flex-col border-l border-border bg-sidebar">
      {/* Header */}
      <div className="border-b border-sidebar-border px-4 py-3">
        <h2 className="text-sm font-semibold text-sidebar-foreground">Plugins</h2>
      </div>

      {/* Search input */}
      <div className="border-b border-sidebar-border p-2">
        <div className="relative">
          <Search className="absolute left-2.5 top-1/2 h-4 w-4 -translate-y-1/2 text-muted-foreground" />
          <input
            type="text"
            placeholder="Search plugins..."
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            className="h-8 w-full rounded-md bg-input pl-8 pr-3 text-sm placeholder:text-muted-foreground focus:outline-none focus:ring-1 focus:ring-ring"
            aria-label="Search plugins"
          />
        </div>
      </div>

      {/* Type filter buttons */}
      <div className="flex items-center gap-1 border-b border-sidebar-border p-2">
        <Button
          variant={selectedType === "all" ? "secondary" : "ghost"}
          size="sm"
          className="h-7 text-xs"
          onClick={() => setSelectedType("all")}
        >
          <Sparkles className="mr-1 h-3 w-3" />
          All
        </Button>
        <Button
          variant={selectedType === "effect" ? "secondary" : "ghost"}
          size="sm"
          className="h-7 text-xs"
          onClick={() => setSelectedType("effect")}
        >
          <Sliders className="mr-1 h-3 w-3" />
          FX
        </Button>
        <Button
          variant={selectedType === "instrument" ? "secondary" : "ghost"}
          size="sm"
          className="h-7 text-xs"
          onClick={() => setSelectedType("instrument")}
        >
          <Music className="mr-1 h-3 w-3" />
          Inst
        </Button>
      </div>

      {/* Plugin list */}
      <div className="flex-1 overflow-auto p-2">
        <div className="flex flex-col gap-1">
          {filteredPlugins.map((plugin) => {
            const Icon = typeIcons[plugin.type]
            return (
              <div
                key={plugin.id}
                className="group flex cursor-grab items-center gap-2 rounded-md px-2 py-2 transition-colors hover:bg-sidebar-accent active:cursor-grabbing"
                draggable
                // TODO: Implement drag-to-canvas functionality
                // onDragStart={(e) => e.dataTransfer.setData("plugin", JSON.stringify(plugin))}
              >
                <Icon className={cn("h-4 w-4", typeColors[plugin.type])} />
                <div className="flex-1 min-w-0">
                  <div className="truncate text-sm font-medium">{plugin.name}</div>
                  <div className="flex items-center gap-2 text-xs text-muted-foreground">
                    <span className="truncate">{plugin.vendor}</span>
                    <span>•</span>
                    <span>{plugin.category}</span>
                  </div>
                </div>
              </div>
            )
          })}
        </div>

        {/* Empty state */}
        {filteredPlugins.length === 0 && (
          <div className="flex flex-col items-center justify-center py-8 text-muted-foreground">
            <Filter className="mb-2 h-8 w-8" />
            <p className="text-sm">No plugins found</p>
          </div>
        )}
      </div>

      {/* Footer stats */}
      <div className="border-t border-sidebar-border px-4 py-2 text-xs text-muted-foreground">
        {filteredPlugins.length} plugins available
      </div>
    </div>
  )
}
