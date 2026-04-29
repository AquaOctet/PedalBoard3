/**
 * =============================================================================
 * TRANSPORT BAR
 * =============================================================================
 * 
 * Top header bar displaying system status and audio configuration.
 * 
 * LAYOUT (left to right):
 * -----------------------
 * [Logo] | [Driver/Rate] [Input Device] [Output Device] | [CPU] [Latency] [MIDI] [OSC] | [Mute] [Settings]
 * 
 * SECTIONS:
 * ---------
 * 1. Branding: PedalBoard2 logo and name
 * 2. Audio Info: Current driver, sample rate, input/output devices with active channels
 * 3. Status: CPU usage, latency, MIDI activity, OSC connection status
 * 4. Controls: Master mute, audio settings dialog
 * 
 * AUDIO SETTINGS:
 * ---------------
 * Settings are stored in local state and passed to AudioSettingsDialog.
 * Changes in the dialog are applied back via onSettingsChange callback.
 * 
 * In a real implementation:
 * - Settings would be persisted to app config file
 * - Changing settings would reconfigure the audio backend
 * - Device lists would come from OS audio API enumeration
 * - CPU/latency would be real-time measurements from audio engine
 * 
 * LATENCY CALCULATION:
 * --------------------
 * Round-trip latency = (bufferSize / sampleRate) * 1000 * 2 ms
 * The "* 2" accounts for both input and output buffer passes.
 * 
 * INTEGRATION POINTS:
 * -------------------
 * To connect to a real audio backend:
 * 1. Replace DEVICE_NAMES with dynamic device enumeration
 * 2. Add WebSocket/IPC for real-time CPU and latency monitoring
 * 3. Implement actual MIDI input activity detection
 * 4. Add OSC server connection status
 */

"use client"

import { useState } from "react"
import { Cpu, Activity, Wifi, Volume2, VolumeX, Mic, MonitorSpeaker } from "lucide-react"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"
import { AudioSettingsDialog } from "./audio-settings-dialog"

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

interface AudioSettings {
  driver: string
  inputDevice: string
  outputDevice: string
  sampleRate: string
  bufferSize: string
  inputChannels: number[]
  outputChannels: number[]
  inputGain: number
  outputGain: number
  exclusiveMode: boolean
}

// =============================================================================
// MOCK DATA - Replace with real device enumeration
// =============================================================================

/**
 * MOCK: Device ID to display name mapping.
 * In a real app, device names would come from the audio backend's
 * device enumeration API (e.g., ASIO, CoreAudio, WASAPI).
 */
const DEVICE_NAMES: Record<string, string> = {
  "focusrite-in": "Scarlett 2i2",
  "focusrite-out": "Scarlett 2i2",
  "behringer-in": "UMC404HD",
  "behringer-out": "UMC404HD",
  "native-in": "Komplete Audio 6",
  "native-out": "Komplete Audio 6",
  "builtin-mic": "Built-in Mic",
  "builtin-speakers": "Built-in Speakers",
}

/**
 * MOCK: Driver display names.
 */
const DRIVER_NAMES: Record<string, string> = {
  asio: "ASIO",
  wasapi: "WASAPI",
  directsound: "DirectSound",
  coreaudio: "CoreAudio",
}

// =============================================================================
// COMPONENT
// =============================================================================

export function TransportBar() {
  const [isMuted, setIsMuted] = useState(false)
  
  // MOCK: CPU usage would be polled from audio engine
  const [cpuUsage] = useState(23)
  
  // Audio settings state
  const [audioSettings, setAudioSettings] = useState<AudioSettings>({
    driver: "asio",
    inputDevice: "focusrite-in",
    outputDevice: "focusrite-out",
    sampleRate: "48000",
    bufferSize: "256",
    inputChannels: [1, 2],
    outputChannels: [1, 2],
    inputGain: 0,
    outputGain: 0,
    exclusiveMode: true,
  })

  /**
   * Calculate round-trip latency from buffer size and sample rate.
   */
  const getLatency = () => {
    const bufferSize = parseInt(audioSettings.bufferSize)
    const sampleRate = parseInt(audioSettings.sampleRate)
    return ((bufferSize / sampleRate) * 1000 * 2).toFixed(1)
  }

  /**
   * Format sample rate for display (e.g., "48kHz").
   */
  const getSampleRateDisplay = () => {
    return `${parseInt(audioSettings.sampleRate) / 1000}kHz`
  }

  return (
    <header className="flex h-12 items-center justify-between border-b border-border bg-card px-4">
      {/* Left section - Logo and audio info */}
      <div className="flex items-center gap-4">
        {/* App branding */}
        <div className="flex items-center gap-2">
          <div className="flex h-7 w-7 items-center justify-center rounded-md bg-primary">
            <Activity className="h-4 w-4 text-primary-foreground" />
          </div>
          <span className="font-semibold tracking-tight">PedalBoard2</span>
        </div>

        <div className="h-5 w-px bg-border" />

        {/* Audio driver and sample rate badge */}
        <div className="flex items-center gap-3">
          <div className="flex items-center gap-1.5 rounded bg-muted/50 px-2 py-1">
            <span className="text-xs font-semibold text-primary">
              {DRIVER_NAMES[audioSettings.driver]}
            </span>
            <span className="text-xs text-muted-foreground">{getSampleRateDisplay()}</span>
          </div>
          
          {/* Input device info (hidden on small screens) */}
          <div className="hidden items-center gap-1.5 text-xs text-muted-foreground sm:flex">
            <Mic className="h-3.5 w-3.5" />
            <span>{DEVICE_NAMES[audioSettings.inputDevice]}</span>
            <span className="text-primary">[{audioSettings.inputChannels.join(",")}]</span>
          </div>

          {/* Output device info (hidden on small/medium screens) */}
          <div className="hidden items-center gap-1.5 text-xs text-muted-foreground md:flex">
            <MonitorSpeaker className="h-3.5 w-3.5" />
            <span>{DEVICE_NAMES[audioSettings.outputDevice]}</span>
            <span className="text-primary">[{audioSettings.outputChannels.join(",")}]</span>
          </div>
        </div>
      </div>

      {/* Center section - System status */}
      <div className="flex items-center gap-6">
        {/* CPU meter - MOCK: value is static */}
        <div className="flex items-center gap-2">
          <Cpu className="h-4 w-4 text-muted-foreground" />
          <div className="flex items-center gap-1.5">
            <div className="h-1.5 w-20 overflow-hidden rounded-full bg-muted">
              <div
                className={cn(
                  "h-full rounded-full transition-all",
                  cpuUsage > 80 ? "bg-destructive" : cpuUsage > 50 ? "bg-chart-3" : "bg-primary"
                )}
                style={{ width: `${cpuUsage}%` }}
              />
            </div>
            <span className="text-xs font-mono text-muted-foreground">{cpuUsage}%</span>
          </div>
        </div>

        {/* Latency display (hidden on small screens) */}
        <div className="hidden items-center gap-2 md:flex">
          <Activity className="h-4 w-4 text-muted-foreground" />
          <span className="text-xs font-mono text-muted-foreground">{getLatency()}ms</span>
        </div>

        {/* MIDI activity indicator - MOCK: always shows activity */}
        <div className="flex items-center gap-2">
          <span className="text-xs font-semibold text-muted-foreground">MIDI</span>
          <div className="flex gap-0.5">
            <div className="h-2 w-2 rounded-sm bg-primary animate-pulse" />
            <div className="h-2 w-2 rounded-sm bg-chart-3" />
          </div>
        </div>

        {/* OSC status (hidden on small screens) - MOCK: always connected */}
        <div className="hidden items-center gap-2 md:flex">
          <Wifi className="h-4 w-4 text-primary" />
          <span className="text-xs font-mono text-muted-foreground">OSC</span>
        </div>
      </div>

      {/* Right section - Master controls */}
      <div className="flex items-center gap-2">
        {/* Master mute toggle */}
        <Button
          variant="ghost"
          size="icon"
          className="h-8 w-8"
          onClick={() => setIsMuted(!isMuted)}
          aria-label={isMuted ? "Unmute" : "Mute"}
        >
          {isMuted ? (
            <VolumeX className="h-4 w-4 text-destructive" />
          ) : (
            <Volume2 className="h-4 w-4" />
          )}
        </Button>

        {/* Audio settings dialog trigger */}
        <AudioSettingsDialog
          settings={audioSettings}
          onSettingsChange={setAudioSettings}
        />
      </div>
    </header>
  )
}
