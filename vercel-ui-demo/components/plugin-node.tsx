/**
 * =============================================================================
 * PLUGIN NODE
 * =============================================================================
 * 
 * Visual representation of a VST/AU plugin instance on the canvas.
 * 
 * LAYOUT:
 * -------
 * ┌─────────────────────────────────────────┐
 * │ [≡] Plugin Name          [⏻] [⚙] [✕] │  <- Header (draggable by grip icon)
 * ├─────────────────────────────────────────┤
 * │ ● In L               Out L ●           │  <- Input/Output ports
 * │ ● In R               Out R ●           │     (clickable for connections)
 * ├─────────────────────────────────────────┤
 * │ MIDI ● CC 1-8                          │  <- MIDI activity indicator
 * └─────────────────────────────────────────┘
 * 
 * Fixed width: 170px (w-[170px])
 * 
 * PORTS:
 * ------
 * - Input ports (left side): Teal/cyan color (--primary)
 * - Output ports (right side): Yellow/amber color (--chart-3)
 * - Each plugin has L (left) and R (right) channel ports
 * - Port IDs follow the format: "{pluginId}-{in|out}-{L|R}"
 * 
 * PORT INTERACTION:
 * -----------------
 * 1. Click empty port -> Start cable drag (port becomes "active")
 * 2. Click connected port -> Disconnect cable, start dragging from other end
 * 3. Drag to compatible port -> Complete connection
 * 
 * Port compatibility:
 * - Output -> Input: Valid
 * - Input -> Output: Valid
 * - Output -> Output: Invalid
 * - Input -> Input: Invalid
 * - Same plugin: Invalid
 * 
 * VISUAL STATES:
 * --------------
 * - Normal: Outlined port circle
 * - Active (being dragged from): Filled, scaled up
 * - Connected: Filled with solid color
 * - Can connect (valid target): Pulsing animation
 * - Bypassed: Entire node dimmed (opacity-50)
 * 
 * PLUGIN TYPES & COLORS:
 * ----------------------
 * - Effect: Primary color left border
 * - Instrument: Chart-3 (amber) left border
 * - Utility: Chart-2 (blue) left border
 * 
 * EVENT FLOW:
 * -----------
 * Events bubble up to SignalFlowCanvas via callback props:
 * - onDragStart: When user starts dragging the node
 * - onPortMouseDown: When user clicks a port
 * - onPortMouseUp: When user releases on a port
 * - onPortMouseEnter: When cursor enters a port (for connection targeting)
 * - onPortMouseLeave: When cursor leaves a port
 * - onBypassToggle: When user clicks the power button
 * - onRemove: When user clicks the X button
 * - onSettings: When user clicks the settings button (not implemented)
 */

"use client"

import { useState, useRef, useCallback, type MouseEvent } from "react"
import { Power, Settings, Grip, X } from "lucide-react"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

/**
 * Represents a connection port on the plugin node.
 * Port IDs must be unique across all plugins.
 */
export interface Port {
  id: string
  type: "input" | "output"
  channel: "L" | "R"
}

interface PluginNodeProps {
  id: string
  name: string
  type: "effect" | "instrument" | "utility"
  bypassed?: boolean
  x: number
  y: number
  onBypassToggle?: (id: string) => void
  onRemove?: (id: string) => void
  onSettings?: (id: string) => void
  onDragStart?: (id: string, e: MouseEvent) => void
  onDrag?: (id: string, deltaX: number, deltaY: number) => void
  onDragEnd?: (id: string) => void
  onPortMouseDown?: (pluginId: string, port: Port, e: MouseEvent) => void
  onPortMouseUp?: (pluginId: string, port: Port) => void
  onPortMouseEnter?: (pluginId: string, port: Port) => void
  onPortMouseLeave?: () => void
  activePort?: { pluginId: string; port: Port } | null
  connectedPorts?: Set<string>
}

// =============================================================================
// COMPONENT
// =============================================================================

export function PluginNode({
  id,
  name,
  type,
  bypassed = false,
  onBypassToggle,
  onRemove,
  onSettings,
  onDragStart,
  onPortMouseDown,
  onPortMouseUp,
  onPortMouseEnter,
  onPortMouseLeave,
  activePort,
  connectedPorts = new Set(),
}: PluginNodeProps) {
  const [isHovered, setIsHovered] = useState(false)
  const nodeRef = useRef<HTMLDivElement>(null)

  // Color mapping for plugin types (applied to left border)
  const typeColors = {
    effect: "border-l-primary",
    instrument: "border-l-chart-3",
    utility: "border-l-chart-2",
  }

  // Generate port definitions for this plugin
  // Each plugin has 2 inputs (L/R) and 2 outputs (L/R)
  const ports: Port[] = [
    { id: `${id}-in-L`, type: "input", channel: "L" },
    { id: `${id}-in-R`, type: "input", channel: "R" },
    { id: `${id}-out-L`, type: "output", channel: "L" },
    { id: `${id}-out-R`, type: "output", channel: "R" },
  ]

  /**
   * Handle mousedown on the node body (not ports or buttons).
   * Initiates drag operation.
   */
  const handleMouseDown = useCallback(
    (e: MouseEvent) => {
      // Don't drag if clicking buttons or ports
      if ((e.target as HTMLElement).closest("button")) return
      if ((e.target as HTMLElement).closest("[data-port]")) return
      e.preventDefault()
      onDragStart?.(id, e)
    },
    [id, onDragStart]
  )

  /**
   * Handle mousedown on a port.
   * Stops propagation to prevent node dragging.
   */
  const handlePortMouseDown = useCallback(
    (port: Port, e: MouseEvent) => {
      e.stopPropagation()
      e.preventDefault()
      onPortMouseDown?.(id, port, e)
    },
    [id, onPortMouseDown]
  )

  /**
   * Handle mouseup on a port.
   * Used for completing connections.
   */
  const handlePortMouseUp = useCallback(
    (port: Port) => {
      onPortMouseUp?.(id, port)
    },
    [id, onPortMouseUp]
  )

  // --- PORT STATE HELPERS ---

  /** Check if this port is the currently active/dragging port */
  const isPortActive = (port: Port) => {
    return activePort?.pluginId === id && activePort?.port.id === port.id
  }

  /** Check if this port has an existing connection */
  const isPortConnected = (port: Port) => {
    return connectedPorts.has(port.id)
  }

  /** Check if this port is a valid connection target for the active port */
  const canConnect = (port: Port) => {
    if (!activePort) return false
    // Can only connect output to input (or vice versa)
    if (activePort.port.type === "output" && port.type === "input") return true
    if (activePort.port.type === "input" && port.type === "output") return true
    return false
  }

  return (
    <div
      ref={nodeRef}
      className={cn(
        "group relative flex flex-col rounded-md border border-border bg-card transition-all select-none",
        "border-l-4 w-[170px]",
        typeColors[type],
        bypassed && "opacity-50",
        isHovered && "border-primary/50 shadow-lg shadow-primary/10"
      )}
      onMouseEnter={() => setIsHovered(true)}
      onMouseLeave={() => setIsHovered(false)}
      onMouseDown={handleMouseDown}
    >
      {/* Header - draggable area */}
      <div className="flex items-center gap-2 border-b border-border px-3 py-2">
        <Grip className="h-4 w-4 cursor-grab active:cursor-grabbing text-muted-foreground" />
        <span className="flex-1 truncate text-sm font-medium">{name}</span>
        <div className="flex items-center gap-1">
          {/* Bypass toggle */}
          <Button
            variant="ghost"
            size="icon"
            className="h-6 w-6"
            onClick={() => onBypassToggle?.(id)}
            aria-label={bypassed ? "Enable plugin" : "Bypass plugin"}
          >
            <Power
              className={cn(
                "h-3.5 w-3.5",
                bypassed ? "text-muted-foreground" : "text-primary"
              )}
            />
          </Button>
          {/* Settings button (not implemented) */}
          <Button
            variant="ghost"
            size="icon"
            className="h-6 w-6"
            onClick={() => onSettings?.(id)}
            aria-label="Plugin settings"
          >
            <Settings className="h-3.5 w-3.5 text-muted-foreground" />
          </Button>
          {/* Remove button (visible on hover) */}
          <Button
            variant="ghost"
            size="icon"
            className="h-6 w-6 opacity-0 group-hover:opacity-100"
            onClick={() => onRemove?.(id)}
            aria-label="Remove plugin"
          >
            <X className="h-3.5 w-3.5 text-muted-foreground hover:text-destructive" />
          </Button>
        </div>
      </div>

      {/* Connection ports area */}
      <div className="flex items-center justify-between px-3 py-3">
        {/* Input ports (left side) */}
        <div className="flex flex-col gap-1.5">
          {ports
            .filter((p) => p.type === "input")
            .map((port) => (
              <div key={port.id} className="flex items-center gap-1.5">
                <div
                  data-port={port.id}
                  className={cn(
                    "h-4 w-4 rounded-full border-2 cursor-pointer transition-all",
                    isPortActive(port)
                      ? "bg-primary border-primary scale-125"
                      : isPortConnected(port)
                      ? "bg-primary border-primary"
                      : canConnect(port)
                      ? "bg-primary/50 border-primary animate-pulse scale-125"
                      : "bg-card border-primary hover:bg-primary/30 hover:scale-110"
                  )}
                  onMouseDown={(e) => handlePortMouseDown(port, e)}
                  onMouseUp={() => handlePortMouseUp(port)}
                  onMouseEnter={() => onPortMouseEnter?.(id, port)}
                  onMouseLeave={() => onPortMouseLeave?.()}
                />
                <span className="text-xs text-muted-foreground">In {port.channel}</span>
              </div>
            ))}
        </div>

        {/* Output ports (right side) */}
        <div className="flex flex-col gap-1.5 items-end">
          {ports
            .filter((p) => p.type === "output")
            .map((port) => (
              <div key={port.id} className="flex items-center gap-1.5">
                <span className="text-xs text-muted-foreground">Out {port.channel}</span>
                <div
                  data-port={port.id}
                  className={cn(
                    "h-4 w-4 rounded-full border-2 cursor-pointer transition-all",
                    isPortActive(port)
                      ? "bg-chart-3 border-chart-3 scale-125"
                      : isPortConnected(port)
                      ? "bg-chart-3 border-chart-3"
                      : canConnect(port)
                      ? "bg-chart-3/50 border-chart-3 animate-pulse scale-125"
                      : "bg-card border-chart-3 hover:bg-chart-3/30 hover:scale-110"
                  )}
                  onMouseDown={(e) => handlePortMouseDown(port, e)}
                  onMouseUp={() => handlePortMouseUp(port)}
                  onMouseEnter={() => onPortMouseEnter?.(id, port)}
                  onMouseLeave={() => onPortMouseLeave?.()}
                />
              </div>
            ))}
        </div>
      </div>

      {/* MIDI activity indicator (MOCK: always shows activity) */}
      <div className="flex items-center gap-2 border-t border-border px-3 py-1.5">
        <span className="text-[10px] font-mono text-muted-foreground">MIDI</span>
        <div className="h-1.5 w-1.5 rounded-full bg-chart-4 animate-pulse" />
        <span className="text-[10px] font-mono text-muted-foreground ml-auto">CC 1-8</span>
      </div>
    </div>
  )
}
