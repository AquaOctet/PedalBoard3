/**
 * =============================================================================
 * AUDIO SETTINGS DIALOG
 * =============================================================================
 * 
 * Modal dialog for configuring audio driver and device settings.
 * Opened via the gear icon in TransportBar.
 * 
 * SETTINGS AVAILABLE:
 * -------------------
 * 1. Audio Driver: ASIO (Windows), CoreAudio (macOS), WASAPI, DirectSound
 * 2. Input Device: Microphone/interface selection with channel toggles
 * 3. Output Device: Speaker/interface selection with channel toggles
 * 4. Sample Rate: 44.1kHz to 192kHz
 * 5. Buffer Size: 32 to 2048 samples (affects latency)
 * 6. Gain Controls: Input/output gain adjustment (-24 to +24 dB)
 * 7. Exclusive Mode: Dedicated device access (ASIO-style)
 * 
 * LATENCY CALCULATION:
 * --------------------
 * Round-trip latency = (bufferSize / sampleRate) * 1000 * 2 ms
 * 
 * Example calculations:
 * - 128 samples @ 44.1kHz = 5.8ms
 * - 256 samples @ 48kHz = 10.67ms
 * - 512 samples @ 96kHz = 10.67ms
 * 
 * CHANNEL SELECTION:
 * ------------------
 * Users can enable/disable individual channels (1-6) for multi-channel interfaces.
 * This is useful for:
 * - Using specific inputs (e.g., only channels 1-2 for stereo)
 * - Routing different instruments to different inputs
 * - Outputting to specific speaker pairs
 * 
 * WORKFLOW:
 * ---------
 * 1. User opens dialog -> localSettings initialized from props
 * 2. User makes changes -> localSettings updated
 * 3. User clicks "Apply Settings" -> onSettingsChange called with localSettings
 * 4. Dialog closes, TransportBar receives new settings
 * 
 * INTEGRATION POINTS:
 * -------------------
 * To connect to a real audio backend:
 * 1. Replace DEVICES arrays with dynamic enumeration from audio API
 * 2. Filter devices based on selected driver
 * 3. Validate settings against device capabilities
 * 4. Restart audio stream with new settings when applied
 * 5. Handle errors (device not available, settings not supported)
 */

"use client"

import { useState } from "react"
import { Settings, Mic, MonitorSpeaker, HardDrive } from "lucide-react"
import { Button } from "@/components/ui/button"
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { Slider } from "@/components/ui/slider"
import { Label } from "@/components/ui/label"
import { Switch } from "@/components/ui/switch"
import { cn } from "@/lib/utils"

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
 * MOCK: Available audio drivers.
 * In a real app, available drivers depend on the operating system:
 * - Windows: ASIO (low latency), WASAPI, DirectSound
 * - macOS: CoreAudio
 * - Linux: ALSA, JACK, PulseAudio
 */
const DRIVERS = [
  { id: "asio", name: "ASIO" },
  { id: "wasapi", name: "WASAPI" },
  { id: "directsound", name: "DirectSound" },
  { id: "coreaudio", name: "CoreAudio" },
]

/**
 * MOCK: Available audio devices.
 * In a real app, these would be enumerated from the OS audio API
 * and filtered based on the selected driver.
 * 
 * Device properties that could be added:
 * - channelCount: Number of available channels
 * - sampleRates: Supported sample rates
 * - isDefault: Whether this is the system default
 */
const DEVICES = {
  input: [
    { id: "focusrite-in", name: "Focusrite Scarlett 2i2" },
    { id: "behringer-in", name: "Behringer UMC404HD" },
    { id: "native-in", name: "Native Instruments Komplete Audio 6" },
    { id: "builtin-mic", name: "Built-in Microphone" },
  ],
  output: [
    { id: "focusrite-out", name: "Focusrite Scarlett 2i2" },
    { id: "behringer-out", name: "Behringer UMC404HD" },
    { id: "native-out", name: "Native Instruments Komplete Audio 6" },
    { id: "builtin-speakers", name: "Built-in Speakers" },
  ],
}

/**
 * MOCK: Available sample rates.
 * Common rates:
 * - 44100 Hz: CD quality
 * - 48000 Hz: Professional audio/video standard
 * - 96000 Hz: High-resolution audio
 * - 192000 Hz: Ultra high-resolution
 */
const SAMPLE_RATES = ["44100", "48000", "88200", "96000", "176400", "192000"]

/**
 * MOCK: Available buffer sizes.
 * Smaller buffers = lower latency but higher CPU usage and risk of glitches.
 * Larger buffers = higher latency but more stable and lower CPU.
 * 
 * Typical use cases:
 * - 64-128: Live performance, real-time processing
 * - 256-512: Recording, mixing
 * - 1024-2048: Mastering, playback only
 */
const BUFFER_SIZES = ["32", "64", "128", "256", "512", "1024", "2048"]

// =============================================================================
// COMPONENT PROPS
// =============================================================================

interface AudioSettingsDialogProps {
  settings: AudioSettings
  onSettingsChange: (settings: AudioSettings) => void
}

// =============================================================================
// COMPONENT
// =============================================================================

export function AudioSettingsDialog({ settings, onSettingsChange }: AudioSettingsDialogProps) {
  const [open, setOpen] = useState(false)
  // Local copy of settings for editing (changes aren't applied until "Apply" is clicked)
  const [localSettings, setLocalSettings] = useState<AudioSettings>(settings)

  /**
   * Update a single setting in local state.
   * Uses generic to ensure type safety.
   */
  const handleSettingChange = <K extends keyof AudioSettings>(
    key: K,
    value: AudioSettings[K]
  ) => {
    setLocalSettings((prev) => ({ ...prev, [key]: value }))
  }

  /**
   * Apply settings and close the dialog.
   */
  const handleApply = () => {
    onSettingsChange(localSettings)
    setOpen(false)
  }

  /**
   * Calculate round-trip latency for display.
   */
  const getLatency = () => {
    const bufferSize = parseInt(localSettings.bufferSize)
    const sampleRate = parseInt(localSettings.sampleRate)
    return ((bufferSize / sampleRate) * 1000 * 2).toFixed(1)
  }

  /**
   * Toggle a channel on or off for input or output.
   */
  const toggleChannel = (type: "input" | "output", channel: number) => {
    const key = type === "input" ? "inputChannels" : "outputChannels"
    const channels = localSettings[key]
    if (channels.includes(channel)) {
      handleSettingChange(key, channels.filter((c) => c !== channel))
    } else {
      handleSettingChange(key, [...channels, channel].sort((a, b) => a - b))
    }
  }

  return (
    <Dialog open={open} onOpenChange={setOpen}>
      <DialogTrigger asChild>
        <Button variant="ghost" size="icon" className="h-8 w-8" aria-label="Audio settings">
          <Settings className="h-4 w-4" />
        </Button>
      </DialogTrigger>
      <DialogContent className="max-w-2xl bg-card border-border">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2 text-foreground">
            <HardDrive className="h-5 w-5 text-primary" />
            Audio Settings
          </DialogTitle>
        </DialogHeader>

        <div className="grid gap-6 py-4">
          {/* Driver Selection */}
          <div className="grid gap-2">
            <Label className="text-sm text-muted-foreground">Audio Driver</Label>
            <Select
              value={localSettings.driver}
              onValueChange={(value) => handleSettingChange("driver", value)}
            >
              <SelectTrigger className="bg-input border-border">
                <SelectValue />
              </SelectTrigger>
              <SelectContent className="bg-popover border-border">
                {DRIVERS.map((driver) => (
                  <SelectItem key={driver.id} value={driver.id}>
                    {driver.name}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>

          {/* Input/Output Devices - Side by side */}
          <div className="grid gap-4 sm:grid-cols-2">
            {/* Input Device Section */}
            <div className="grid gap-2">
              <Label className="flex items-center gap-2 text-sm text-muted-foreground">
                <Mic className="h-4 w-4" />
                Input Device
              </Label>
              <Select
                value={localSettings.inputDevice}
                onValueChange={(value) => handleSettingChange("inputDevice", value)}
              >
                <SelectTrigger className="bg-input border-border">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent className="bg-popover border-border">
                  {DEVICES.input.map((device) => (
                    <SelectItem key={device.id} value={device.id}>
                      {device.name}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>

              {/* Input Channel Toggles */}
              <div className="mt-2">
                <Label className="text-xs text-muted-foreground">Active Channels</Label>
                <div className="mt-1.5 flex flex-wrap gap-1.5">
                  {[1, 2, 3, 4, 5, 6].map((ch) => (
                    <button
                      key={ch}
                      onClick={() => toggleChannel("input", ch)}
                      className={cn(
                        "flex h-7 w-7 items-center justify-center rounded text-xs font-mono transition-colors",
                        localSettings.inputChannels.includes(ch)
                          ? "bg-primary text-primary-foreground"
                          : "bg-muted text-muted-foreground hover:bg-muted/80"
                      )}
                      aria-label={`Channel ${ch} ${localSettings.inputChannels.includes(ch) ? "enabled" : "disabled"}`}
                    >
                      {ch}
                    </button>
                  ))}
                </div>
              </div>

              {/* Input Gain Slider */}
              <div className="mt-2">
                <div className="flex items-center justify-between">
                  <Label className="text-xs text-muted-foreground">Input Gain</Label>
                  <span className="text-xs font-mono text-primary">
                    {localSettings.inputGain > 0 ? "+" : ""}
                    {localSettings.inputGain} dB
                  </span>
                </div>
                <Slider
                  value={[localSettings.inputGain]}
                  onValueChange={([value]) => handleSettingChange("inputGain", value)}
                  min={-24}
                  max={24}
                  step={0.5}
                  className="mt-2"
                />
              </div>
            </div>

            {/* Output Device Section */}
            <div className="grid gap-2">
              <Label className="flex items-center gap-2 text-sm text-muted-foreground">
                <MonitorSpeaker className="h-4 w-4" />
                Output Device
              </Label>
              <Select
                value={localSettings.outputDevice}
                onValueChange={(value) => handleSettingChange("outputDevice", value)}
              >
                <SelectTrigger className="bg-input border-border">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent className="bg-popover border-border">
                  {DEVICES.output.map((device) => (
                    <SelectItem key={device.id} value={device.id}>
                      {device.name}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>

              {/* Output Channel Toggles */}
              <div className="mt-2">
                <Label className="text-xs text-muted-foreground">Active Channels</Label>
                <div className="mt-1.5 flex flex-wrap gap-1.5">
                  {[1, 2, 3, 4, 5, 6].map((ch) => (
                    <button
                      key={ch}
                      onClick={() => toggleChannel("output", ch)}
                      className={cn(
                        "flex h-7 w-7 items-center justify-center rounded text-xs font-mono transition-colors",
                        localSettings.outputChannels.includes(ch)
                          ? "bg-primary text-primary-foreground"
                          : "bg-muted text-muted-foreground hover:bg-muted/80"
                      )}
                      aria-label={`Channel ${ch} ${localSettings.outputChannels.includes(ch) ? "enabled" : "disabled"}`}
                    >
                      {ch}
                    </button>
                  ))}
                </div>
              </div>

              {/* Output Gain Slider */}
              <div className="mt-2">
                <div className="flex items-center justify-between">
                  <Label className="text-xs text-muted-foreground">Output Gain</Label>
                  <span className="text-xs font-mono text-primary">
                    {localSettings.outputGain > 0 ? "+" : ""}
                    {localSettings.outputGain} dB
                  </span>
                </div>
                <Slider
                  value={[localSettings.outputGain]}
                  onValueChange={([value]) => handleSettingChange("outputGain", value)}
                  min={-24}
                  max={24}
                  step={0.5}
                  className="mt-2"
                />
              </div>
            </div>
          </div>

          {/* Sample Rate and Buffer Size - Side by side */}
          <div className="grid gap-4 sm:grid-cols-2">
            <div className="grid gap-2">
              <Label className="text-sm text-muted-foreground">Sample Rate</Label>
              <Select
                value={localSettings.sampleRate}
                onValueChange={(value) => handleSettingChange("sampleRate", value)}
              >
                <SelectTrigger className="bg-input border-border">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent className="bg-popover border-border">
                  {SAMPLE_RATES.map((rate) => (
                    <SelectItem key={rate} value={rate}>
                      {parseInt(rate) / 1000} kHz
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            <div className="grid gap-2">
              <Label className="text-sm text-muted-foreground">Buffer Size</Label>
              <Select
                value={localSettings.bufferSize}
                onValueChange={(value) => handleSettingChange("bufferSize", value)}
              >
                <SelectTrigger className="bg-input border-border">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent className="bg-popover border-border">
                  {BUFFER_SIZES.map((size) => (
                    <SelectItem key={size} value={size}>
                      {size} samples
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
          </div>

          {/* Latency Display and Exclusive Mode */}
          <div className="flex items-center justify-between rounded-lg bg-muted/50 p-4">
            <div className="flex items-center gap-4">
              <div>
                <span className="text-xs text-muted-foreground">Round-trip Latency</span>
                <p className="text-lg font-mono text-primary">{getLatency()} ms</p>
              </div>
            </div>

            <div className="flex items-center gap-3">
              <Label htmlFor="exclusive-mode" className="text-sm text-muted-foreground">
                Exclusive Mode
              </Label>
              <Switch
                id="exclusive-mode"
                checked={localSettings.exclusiveMode}
                onCheckedChange={(checked) => handleSettingChange("exclusiveMode", checked)}
              />
            </div>
          </div>
        </div>

        {/* Dialog Actions */}
        <div className="flex justify-end gap-2">
          <Button variant="outline" onClick={() => setOpen(false)}>
            Cancel
          </Button>
          <Button onClick={handleApply}>
            Apply Settings
          </Button>
        </div>
      </DialogContent>
    </Dialog>
  )
}
