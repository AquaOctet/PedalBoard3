/**
 * =============================================================================
 * SIGNAL FLOW CANVAS
 * =============================================================================
 * 
 * The main workspace for visual audio routing. Users can:
 * - Drag plugin nodes to reposition them
 * - Click on ports to create/remove cable connections
 * - Zoom in/out for different levels of detail
 * - Select and delete cables
 * 
 * COORDINATE SYSTEM:
 * ------------------
 * - Canvas origin (0,0) is at top-left
 * - Plugin positions are in canvas coordinates (not screen coordinates)
 * - Zoom is applied via CSS transform: scale()
 * - Mouse positions are converted from screen to canvas coordinates using zoom factor
 * 
 * CABLE CONNECTION FLOW:
 * ----------------------
 * 1. User clicks (mouseDown) on a port -> sets activePort state
 * 2. User drags cursor -> cable preview follows mouse position
 * 3. User hovers over target port -> sets hoveredPort state
 * 4. User releases mouse (mouseUp) -> if hoveredPort is valid, create connection
 * 
 * Connection validation rules:
 * - Output ports can only connect to input ports (and vice versa)
 * - Cannot connect a plugin to itself
 * - Duplicate connections are prevented
 * 
 * PERFORMANCE OPTIMIZATIONS:
 * --------------------------
 * - Plugin positions are stored in a ref (pluginsRef) for immediate updates during drag
 * - State updates are throttled via requestAnimationFrame to prevent excessive re-renders
 * - SVG cable paths use hardware-accelerated transforms
 * 
 * PORT POSITION CALCULATION:
 * --------------------------
 * getPortPosition() calculates the absolute position of a port for cable rendering:
 *   - Input ports are on the left side of the node (x = plugin.x)
 *   - Output ports are on the right side (x = plugin.x + nodeWidth)
 *   - Y positions are offset from plugin.y based on channel (L/R)
 * 
 * If adjusting PluginNode layout, update these constants:
 *   - nodeWidth: 170px (matches PluginNode w-[170px])
 *   - portYBase: 75px (vertical offset to port area)
 *   - portSpacing: 20px (spacing between L and R ports)
 */

"use client"

import { useState, useCallback, useRef, useEffect, type MouseEvent } from "react"
import { Plus, Minus, Maximize2, Trash2 } from "lucide-react"
import { Button } from "@/components/ui/button"
import { PluginNode, type Port } from "./plugin-node"

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

interface Plugin {
  id: string
  name: string
  type: "effect" | "instrument" | "utility"
  bypassed: boolean
  x: number
  y: number
}

interface Connection {
  id: string
  fromPluginId: string
  fromPort: Port
  toPluginId: string
  toPort: Port
}

// =============================================================================
// MOCK DATA - Replace with real data from backend/state management
// =============================================================================

/**
 * MOCK: Initial plugin instances on the canvas.
 * In a real implementation, this would come from:
 * - Loaded patch file
 * - Backend state sync
 * - User adding plugins from PluginBrowser
 */
const initialPlugins: Plugin[] = [
  { id: "1", name: "Audio Input", type: "utility", bypassed: false, x: 50, y: 120 },
  { id: "2", name: "Valhalla Shimmer", type: "effect", bypassed: false, x: 300, y: 50 },
  { id: "3", name: "FabFilter Pro-Q 3", type: "effect", bypassed: false, x: 300, y: 250 },
  { id: "4", name: "Soundtoys Decapitator", type: "effect", bypassed: true, x: 550, y: 150 },
  { id: "5", name: "Audio Output", type: "utility", bypassed: false, x: 800, y: 120 },
]

/**
 * MOCK: Initial cable connections.
 * Connection format: output port -> input port
 * 
 * This example signal flow:
 *   Audio Input (L) -> Shimmer (L) -> Decapitator (L) -> Audio Output (L)
 *   Audio Input (R) -> Pro-Q 3 (R) -> Decapitator (R)
 */
const initialConnections: Connection[] = [
  { id: "c1", fromPluginId: "1", fromPort: { id: "1-out-L", type: "output", channel: "L" }, toPluginId: "2", toPort: { id: "2-in-L", type: "input", channel: "L" } },
  { id: "c2", fromPluginId: "1", fromPort: { id: "1-out-R", type: "output", channel: "R" }, toPluginId: "3", toPort: { id: "3-in-R", type: "input", channel: "R" } },
  { id: "c3", fromPluginId: "2", fromPort: { id: "2-out-L", type: "output", channel: "L" }, toPluginId: "4", toPort: { id: "4-in-L", type: "input", channel: "L" } },
  { id: "c4", fromPluginId: "3", fromPort: { id: "3-out-R", type: "output", channel: "R" }, toPluginId: "4", toPort: { id: "4-in-R", type: "input", channel: "R" } },
  { id: "c5", fromPluginId: "4", fromPort: { id: "4-out-L", type: "output", channel: "L" }, toPluginId: "5", toPort: { id: "5-in-L", type: "input", channel: "L" } },
]

// =============================================================================
// COMPONENT
// =============================================================================

export function SignalFlowCanvas() {
  // --- STATE ---
  const [plugins, setPlugins] = useState<Plugin[]>(initialPlugins)
  const [connections, setConnections] = useState<Connection[]>(initialConnections)
  const [zoom, setZoom] = useState(100)
  const [draggingPlugin, setDraggingPlugin] = useState<string | null>(null)
  const [dragOffset, setDragOffset] = useState({ x: 0, y: 0 })
  const [activePort, setActivePort] = useState<{ pluginId: string; port: Port } | null>(null)
  const [hoveredPort, setHoveredPort] = useState<{ pluginId: string; port: Port } | null>(null)
  const [mousePos, setMousePos] = useState({ x: 0, y: 0 })
  const [selectedConnection, setSelectedConnection] = useState<string | null>(null)

  // --- REFS (for performance - avoid stale closures in callbacks) ---
  const activePortRef = useRef<{ pluginId: string; port: Port } | null>(null)
  const hoveredPortRef = useRef<{ pluginId: string; port: Port } | null>(null)
  const canvasRef = useRef<HTMLDivElement>(null)
  const connectionsRef = useRef<Connection[]>(connections)
  const pluginsRef = useRef<Plugin[]>(plugins)
  const animationFrameRef = useRef<number | null>(null)

  // Keep refs in sync with state
  activePortRef.current = activePort
  connectionsRef.current = connections
  hoveredPortRef.current = hoveredPort
  pluginsRef.current = plugins

  // --- DERIVED STATE ---
  // Set of port IDs that have active connections (for visual feedback)
  const connectedPorts = new Set<string>()
  connections.forEach((conn) => {
    connectedPorts.add(conn.fromPort.id)
    connectedPorts.add(conn.toPort.id)
  })

  // =============================================================================
  // EVENT HANDLERS
  // =============================================================================

  /**
   * Toggle the bypass state of a plugin.
   * Bypassed plugins are dimmed and their cables are shown dashed.
   */
  const handleBypassToggle = (id: string) => {
    setPlugins(
      plugins.map((p) => (p.id === id ? { ...p, bypassed: !p.bypassed } : p))
    )
  }

  /**
   * Remove a plugin and all its connections from the canvas.
   */
  const handleRemove = (id: string) => {
    setPlugins(plugins.filter((p) => p.id !== id))
    setConnections(connections.filter((c) => c.fromPluginId !== id && c.toPluginId !== id))
  }

  /**
   * Start dragging a plugin node.
   * Calculates the offset from the cursor to the plugin's top-left corner.
   */
  const handleDragStart = useCallback((id: string, e: MouseEvent) => {
    const plugin = plugins.find((p) => p.id === id)
    if (!plugin || !canvasRef.current) return

    const rect = canvasRef.current.getBoundingClientRect()
    const scale = zoom / 100
    const offsetX = (e.clientX - rect.left) / scale - plugin.x
    const offsetY = (e.clientY - rect.top) / scale - plugin.y

    setDraggingPlugin(id)
    setDragOffset({ x: offsetX, y: offsetY })
  }, [plugins, zoom])

  /**
   * Handle mouse movement for both plugin dragging and cable dragging.
   * Uses refs and requestAnimationFrame for smooth performance.
   */
  const handleMouseMove = useCallback((e: MouseEvent) => {
    if (!canvasRef.current) return

    const rect = canvasRef.current.getBoundingClientRect()
    const scale = zoom / 100
    const x = (e.clientX - rect.left) / scale
    const y = (e.clientY - rect.top) / scale

    setMousePos({ x, y })

    if (draggingPlugin) {
      const newX = Math.max(0, x - dragOffset.x)
      const newY = Math.max(0, y - dragOffset.y)
      
      // Update ref immediately for smooth cable rendering
      pluginsRef.current = pluginsRef.current.map((p) =>
        p.id === draggingPlugin ? { ...p, x: newX, y: newY } : p
      )
      
      // Throttle state updates with requestAnimationFrame
      if (animationFrameRef.current === null) {
        animationFrameRef.current = requestAnimationFrame(() => {
          setPlugins([...pluginsRef.current])
          animationFrameRef.current = null
        })
      }
    }
  }, [draggingPlugin, dragOffset, zoom])

  /**
   * Handle mouse release - complete plugin drag or cable connection.
   * Connection logic uses refs to avoid stale closure issues.
   */
  const handleMouseUp = useCallback(() => {
    setDraggingPlugin(null)
    
    const currentActivePort = activePortRef.current
    const currentHoveredPort = hoveredPortRef.current
    const currentConnections = connectionsRef.current
    
    if (currentActivePort && currentHoveredPort) {
      // Try to complete connection
      if (currentActivePort.pluginId !== currentHoveredPort.pluginId) {
        let fromPluginId: string, fromPort: Port, toPluginId: string, toPort: Port

        if (currentActivePort.port.type === "output" && currentHoveredPort.port.type === "input") {
          fromPluginId = currentActivePort.pluginId
          fromPort = currentActivePort.port
          toPluginId = currentHoveredPort.pluginId
          toPort = currentHoveredPort.port
        } else if (currentActivePort.port.type === "input" && currentHoveredPort.port.type === "output") {
          fromPluginId = currentHoveredPort.pluginId
          fromPort = currentHoveredPort.port
          toPluginId = currentActivePort.pluginId
          toPort = currentActivePort.port
        } else {
          setActivePort(null)
          return
        }

        // Check if connection already exists
        const exists = currentConnections.some(
          (c) => c.fromPort.id === fromPort.id && c.toPort.id === toPort.id
        )
        if (!exists) {
          const newConnection: Connection = {
            id: `c-${Date.now()}`,
            fromPluginId,
            fromPort,
            toPluginId,
            toPort,
          }
          setConnections([...currentConnections, newConnection])
        }
      }
    }
    
    setActivePort(null)
  }, [])

  /**
   * Handle clicking on a port to start a new connection or disconnect an existing one.
   */
  const handlePortMouseDown = useCallback((pluginId: string, port: Port, e: MouseEvent) => {
    const currentConnections = connectionsRef.current
    // Check if clicking on an already connected port to disconnect
    const existingConnection = currentConnections.find(
      (c) => c.fromPort.id === port.id || c.toPort.id === port.id
    )
    if (existingConnection) {
      // Remove the connection
      setConnections(currentConnections.filter((c) => c.id !== existingConnection.id))
      // Start dragging from the other end
      if (existingConnection.fromPort.id === port.id) {
        setActivePort({ pluginId: existingConnection.toPluginId, port: existingConnection.toPort })
      } else {
        setActivePort({ pluginId: existingConnection.fromPluginId, port: existingConnection.fromPort })
      }
    } else {
      setActivePort({ pluginId, port })
    }

    if (canvasRef.current) {
      const rect = canvasRef.current.getBoundingClientRect()
      const scale = zoom / 100
      setMousePos({
        x: (e.clientX - rect.left) / scale,
        y: (e.clientY - rect.top) / scale,
      })
    }
  }, [zoom])

  /**
   * Track when mouse enters a port (for connection targeting).
   */
  const handlePortMouseEnter = useCallback((pluginId: string, port: Port) => {
    setHoveredPort({ pluginId, port })
  }, [])

  /**
   * Clear hovered port when mouse leaves.
   */
  const handlePortMouseLeave = useCallback(() => {
    setHoveredPort(null)
  }, [])

  /**
   * Handle direct click on a target port (backup for handleMouseUp).
   */
  const handlePortMouseUp = useCallback((pluginId: string, port: Port) => {
    const currentActivePort = activePortRef.current
    const currentConnections = connectionsRef.current
    
    if (!currentActivePort) return
    if (currentActivePort.pluginId === pluginId) return // Same plugin

    // Determine which is output and which is input
    let fromPluginId: string, fromPort: Port, toPluginId: string, toPort: Port

    if (currentActivePort.port.type === "output" && port.type === "input") {
      fromPluginId = currentActivePort.pluginId
      fromPort = currentActivePort.port
      toPluginId = pluginId
      toPort = port
    } else if (currentActivePort.port.type === "input" && port.type === "output") {
      fromPluginId = pluginId
      fromPort = port
      toPluginId = currentActivePort.pluginId
      toPort = currentActivePort.port
    } else {
      setActivePort(null)
      return // Can't connect same types
    }

    // Check if connection already exists
    const exists = currentConnections.some(
      (c) => c.fromPort.id === fromPort.id && c.toPort.id === toPort.id
    )
    if (exists) {
      setActivePort(null)
      return
    }

    // Add new connection
    const newConnection: Connection = {
      id: `c-${Date.now()}`,
      fromPluginId,
      fromPort,
      toPluginId,
      toPort,
    }
    setConnections([...currentConnections, newConnection])
    setActivePort(null)
  }, [])

  // =============================================================================
  // HELPER FUNCTIONS
  // =============================================================================

  /**
   * Calculate the absolute position of a port on the canvas.
   * Used for rendering cable endpoints.
   * 
   * IMPORTANT: These offsets must match the PluginNode component layout.
   */
  const getPortPosition = useCallback((pluginId: string, port: Port) => {
    // Use ref for immediate position during drag
    const plugin = pluginsRef.current.find((p) => p.id === pluginId)
    if (!plugin) return { x: 0, y: 0 }

    const nodeWidth = 170   // Must match PluginNode w-[170px]
    const portYBase = 75    // Base Y offset for ports in the node
    const portSpacing = 20  // Spacing between L and R ports

    if (port.type === "input") {
      return {
        x: plugin.x,
        y: plugin.y + portYBase + (port.channel === "L" ? 0 : portSpacing),
      }
    } else {
      return {
        x: plugin.x + nodeWidth,
        y: plugin.y + portYBase + (port.channel === "L" ? 0 : portSpacing),
      }
    }
  }, [])

  /**
   * Generate a smooth bezier curve path for a cable.
   * The control points create a horizontal "S" curve that looks like a real cable.
   */
  const generateCablePath = (from: { x: number; y: number }, to: { x: number; y: number }) => {
    const dx = to.x - from.x
    const controlOffset = Math.min(Math.abs(dx) * 0.5, 100)
    return `M ${from.x} ${from.y} C ${from.x + controlOffset} ${from.y}, ${to.x - controlOffset} ${to.y}, ${to.x} ${to.y}`
  }

  /**
   * Delete the currently selected connection.
   */
  const handleDeleteConnection = () => {
    if (selectedConnection) {
      setConnections(connections.filter((c) => c.id !== selectedConnection))
      setSelectedConnection(null)
    }
  }

  // =============================================================================
  // KEYBOARD SHORTCUTS
  // =============================================================================

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if ((e.key === "Delete" || e.key === "Backspace") && selectedConnection) {
        handleDeleteConnection()
      }
      if (e.key === "Escape") {
        setActivePort(null)
        setSelectedConnection(null)
      }
    }
    window.addEventListener("keydown", handleKeyDown)
    return () => window.removeEventListener("keydown", handleKeyDown)
  }, [selectedConnection])

  // Cleanup animation frame on unmount
  useEffect(() => {
    return () => {
      if (animationFrameRef.current !== null) {
        cancelAnimationFrame(animationFrameRef.current)
      }
    }
  }, [])

  // =============================================================================
  // RENDER
  // =============================================================================

  return (
    <div
      ref={canvasRef}
      className="relative flex-1 overflow-hidden bg-background cursor-crosshair"
      onMouseMove={handleMouseMove}
      onMouseUp={handleMouseUp}
      onMouseLeave={handleMouseUp}
      onClick={() => setSelectedConnection(null)}
    >
      {/* Grid background - scales with zoom */}
      <div
        className="absolute inset-0 pointer-events-none"
        style={{
          backgroundImage: `
            linear-gradient(to right, oklch(0.20 0.01 250) 1px, transparent 1px),
            linear-gradient(to bottom, oklch(0.20 0.01 250) 1px, transparent 1px)
          `,
          backgroundSize: `${40 * zoom / 100}px ${40 * zoom / 100}px`,
        }}
      />

      {/* SVG layer for cables - positioned behind plugin nodes */}
      <svg
        className="absolute pointer-events-none"
        style={{ 
          transform: `scale(${zoom / 100})`, 
          transformOrigin: "top left",
          top: 0,
          left: 0,
          width: "4000px",
          height: "4000px",
          overflow: "visible"
        }}
      >
        {/* SVG definitions for gradients and filters */}
        <defs>
          <linearGradient id="cableGradient" x1="0%" y1="0%" x2="100%" y2="0%">
            <stop offset="0%" stopColor="oklch(0.75 0.18 80)" />
            <stop offset="100%" stopColor="oklch(0.72 0.19 160)" />
          </linearGradient>
          <linearGradient id="cableGradientSelected" x1="0%" y1="0%" x2="100%" y2="0%">
            <stop offset="0%" stopColor="oklch(0.85 0.22 80)" />
            <stop offset="100%" stopColor="oklch(0.82 0.24 160)" />
          </linearGradient>
          <filter id="glow">
            <feGaussianBlur stdDeviation="2" result="coloredBlur" />
            <feMerge>
              <feMergeNode in="coloredBlur" />
              <feMergeNode in="SourceGraphic" />
            </feMerge>
          </filter>
        </defs>

        {/* Render existing connections */}
        {connections.map((conn) => {
          const fromPos = getPortPosition(conn.fromPluginId, conn.fromPort)
          const toPos = getPortPosition(conn.toPluginId, conn.toPort)
          const fromPlugin = plugins.find((p) => p.id === conn.fromPluginId)
          const isSelected = selectedConnection === conn.id
          const isBypassed = fromPlugin?.bypassed

          return (
            <g key={conn.id}>
              {/* Invisible hitbox for easier clicking */}
              <path
                d={generateCablePath(fromPos, toPos)}
                fill="none"
                stroke="transparent"
                strokeWidth="12"
                className="pointer-events-auto cursor-pointer"
                onClick={(e) => {
                  e.stopPropagation()
                  setSelectedConnection(conn.id)
                }}
              />
              {/* Visible cable */}
              <path
                d={generateCablePath(fromPos, toPos)}
                fill="none"
                stroke={isSelected ? "url(#cableGradientSelected)" : "url(#cableGradient)"}
                strokeWidth={isSelected ? "4" : "3"}
                strokeLinecap="round"
                strokeDasharray={isBypassed ? "8 4" : "none"}
                opacity={isBypassed ? 0.4 : 1}
                filter={isSelected ? "url(#glow)" : "none"}
                className="transition-all duration-150"
              />
            </g>
          )
        })}

        {/* Active drag cable (preview while connecting) */}
        {activePort && (
          <path
            d={generateCablePath(
              getPortPosition(activePort.pluginId, activePort.port),
              mousePos
            )}
            fill="none"
            stroke="oklch(0.72 0.19 160)"
            strokeWidth="2"
            strokeDasharray="6 3"
            opacity="0.7"
            className="animate-pulse"
          />
        )}
      </svg>

      {/* Plugin nodes layer - positioned above cables */}
      <div
        className="absolute inset-0"
        style={{ transform: `scale(${zoom / 100})`, transformOrigin: "top left" }}
      >
        {plugins.map((plugin) => (
          <div
            key={plugin.id}
            className="absolute transition-shadow"
            style={{
              left: plugin.x,
              top: plugin.y,
              cursor: draggingPlugin === plugin.id ? "grabbing" : "grab",
            }}
          >
            <PluginNode
              id={plugin.id}
              name={plugin.name}
              type={plugin.type}
              bypassed={plugin.bypassed}
              x={plugin.x}
              y={plugin.y}
              onBypassToggle={handleBypassToggle}
              onRemove={handleRemove}
              onDragStart={handleDragStart}
              onPortMouseDown={handlePortMouseDown}
              onPortMouseUp={handlePortMouseUp}
              onPortMouseEnter={handlePortMouseEnter}
              onPortMouseLeave={handlePortMouseLeave}
              activePort={activePort}
              connectedPorts={connectedPorts}
            />
          </div>
        ))}
      </div>

      {/* Zoom controls - bottom right corner */}
      <div className="absolute bottom-4 right-4 flex items-center gap-1 rounded-md border border-border bg-card p-1">
        <Button
          variant="ghost"
          size="icon"
          className="h-7 w-7"
          onClick={() => setZoom(Math.max(50, zoom - 10))}
          aria-label="Zoom out"
        >
          <Minus className="h-3.5 w-3.5" />
        </Button>
        <span className="w-12 text-center text-xs font-mono">{zoom}%</span>
        <Button
          variant="ghost"
          size="icon"
          className="h-7 w-7"
          onClick={() => setZoom(Math.min(150, zoom + 10))}
          aria-label="Zoom in"
        >
          <Plus className="h-3.5 w-3.5" />
        </Button>
        <Button
          variant="ghost"
          size="icon"
          className="h-7 w-7"
          onClick={() => setZoom(100)}
          aria-label="Reset zoom"
        >
          <Maximize2 className="h-3.5 w-3.5" />
        </Button>
      </div>

      {/* Selected connection controls */}
      {selectedConnection && (
        <div className="absolute top-4 right-4 flex items-center gap-2 rounded-md border border-border bg-card p-2">
          <span className="text-xs text-muted-foreground">Cable selected</span>
          <Button
            variant="destructive"
            size="sm"
            onClick={handleDeleteConnection}
          >
            <Trash2 className="mr-1 h-3 w-3" />
            Delete
          </Button>
        </div>
      )}

      {/* Add plugin button */}
      <Button
        className="absolute bottom-4 left-4"
        size="sm"
      >
        <Plus className="mr-2 h-4 w-4" />
        Add Plugin
      </Button>

      {/* Connection instructions tooltip */}
      {activePort && (
        <div className="absolute top-4 left-1/2 -translate-x-1/2 rounded-md border border-primary/50 bg-card px-4 py-2 text-sm">
          Drag to a {activePort.port.type === "output" ? "input" : "output"} port to connect, or release to cancel
        </div>
      )}
    </div>
  )
}
