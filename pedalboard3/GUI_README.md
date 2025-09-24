# Pedalboard3 GUI - Modern Audio Plugin Host

## Overview

The Pedalboard3 GUI provides a modern, user-friendly interface for the Pedalboard3 audio plugin host. Built with Rust and egui, it delivers real-time performance with beautiful visualizations.

## Features

### 🎸 Audio Engine Management
- **Device Selection**: Choose input/output audio devices from a visual interface
- **Real-time Status**: Monitor audio engine status with visual indicators
- **Buffer & Sample Rate Control**: Adjust audio settings for optimal performance

### 🔌 Plugin Management
- **Plugin Scanner**: Automatically scan for VST2, VST3, AU, CLAP, and LV2 plugins
- **Plugin Chain**: Drag-and-drop plugin arrangement with visual chain management
- **Parameter Control**: Real-time plugin parameter adjustment with hardware-style knobs and faders
- **Bypass & Enable**: Individual plugin bypass and enable/disable controls

### 📊 Real-time Visualization
- **Waveform Display**: Live audio waveform visualization
- **Spectrum Analyzer**: Real-time frequency spectrum analysis with color-coded levels
- **VU Meters**: Classic VU meters for audio level monitoring
- **CPU Usage**: Live CPU usage monitoring

### 🎛️ Professional Controls
- **Hardware-style UI**: Knobs, faders, and LEDs that mimic real hardware
- **Dark Theme**: Professional dark interface optimized for studio environments
- **Responsive Layout**: Resizable panels and windows for custom workflows

## Quick Start

### Windows Users
1. **Double-click** `pedalboard3-gui.bat` to launch the GUI
2. Or run from command line: `cargo run --bin pedalboard3-gui`

### First-time Setup
1. Click **"🔊 Start Audio Engine"** to initialize audio processing
2. Go to **Audio → Audio Settings** to configure your audio interface
3. Use **Plugins → Scan for Plugins** to find your installed plugins
4. Start creating your audio processing chain!

## Interface Guide

### Main Window
- **Menu Bar**: File, Audio, Plugins, View, and Help menus
- **Central Panel**: Main controls and audio status
- **Status Bar**: Real-time system information (CPU, audio status, plugin count)

### Audio Devices Panel
- **Input Device**: Select your audio input (microphone, interface, etc.)
- **Output Device**: Select your audio output (speakers, headphones, etc.)
- **Device List**: Browse all available audio devices with I/O indicators
- **Refresh**: Update device list after connecting/disconnecting hardware

### Plugin Manager Panel
- **Available Plugins**: Left panel shows all scanned plugins
- **Plugin Chain**: Right panel shows your active plugin chain
- **Search & Filter**: Find plugins by name, vendor, or category
- **Add Plugins**: Click "Add" to insert plugins into your chain
- **Plugin Controls**: Enable/disable, bypass, and parameter adjustment

### Audio Mixer & Visualization Panel
- **Master Controls**: Overall volume and system controls
- **Waveform Display**: Real-time audio waveform
- **Spectrum Analyzer**: Frequency analysis with peak indicators
- **VU Meters**: Left and right channel level meters
- **Demo Generator**: Built-in tone generator for testing

### Settings Panel
- **Sample Rate**: 44.1kHz, 48kHz, 88.2kHz, 96kHz options
- **Buffer Size**: 64 to 1024 samples (lower = less latency, higher CPU usage)

## Demo Mode

The GUI includes a demo mode that works without real audio hardware:

1. **Mock Devices**: Simulated audio devices for testing
2. **Demo Audio Generator**: Built-in sine wave generator with harmonics
3. **Live Visualization**: Real-time waveform and spectrum display
4. **Plugin Simulation**: Test plugin loading and management

## Keyboard Shortcuts

- **Ctrl+N**: New Session
- **Ctrl+O**: Open Session
- **Ctrl+S**: Save Session
- **Space**: Start/Stop Audio Engine
- **F11**: Toggle Fullscreen
- **Escape**: Close current dialog

## Performance Tips

### For Low Latency
- Use smaller buffer sizes (64-256 samples)
- Close unused GUI panels
- Disable plugin visualizations when not needed
- Use high-priority audio devices (ASIO on Windows)

### For Stability
- Use larger buffer sizes (512-1024 samples)
- Keep CPU usage below 70%
- Avoid overloading the plugin chain
- Monitor system resources

## Troubleshooting

### Audio Issues
- **No Audio Devices**: Check that audio drivers are installed
- **Clicks/Pops**: Increase buffer size or sample rate
- **High CPU Usage**: Reduce plugin count or increase buffer size
- **No Sound**: Check device selection and master volume

### Plugin Issues
- **No Plugins Found**: Check plugin installation paths
- **Plugin Won't Load**: Check plugin compatibility (32-bit vs 64-bit)
- **Plugin Crashes**: Use plugin bypass to isolate problematic plugins

### GUI Issues
- **Slow Performance**: Close unused panels, reduce visualization quality
- **Window Issues**: Reset layout from View menu
- **Graphics Problems**: Update graphics drivers

## Development

### Building from Source
```bash
# Install Rust (if not already installed)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Clone and build
git clone [repository]
cd pedalboard3
cargo build --bin pedalboard3-gui --release

# Run
cargo run --bin pedalboard3-gui
```

### Dependencies
- **Rust 1.70+**
- **egui 0.24**: Modern immediate mode GUI framework
- **cpal**: Cross-platform audio library
- **VST SDK**: For VST plugin support

## Architecture

### GUI Components
- **Main App** (`app.rs`): Core application logic and state management
- **Audio Visualizer** (`audio_viz.rs`): Real-time audio visualization components
- **Plugin Manager** (`plugin_manager.rs`): Plugin loading and management interface
- **Components** (`components.rs`): Reusable UI widgets (knobs, faders, LEDs)

### Integration
- **Audio Backend**: Interfaces with `pedalboard-audio` crate
- **Plugin System**: Integrates with `pedalboard-plugins` crate
- **Configuration**: Uses `pedalboard-core` configuration system

## Future Enhancements

- **MIDI Controller Support**: Hardware MIDI controller integration
- **Plugin Editor Windows**: Native plugin editor support
- **Session Management**: Save/load complete sessions
- **Performance Profiling**: Detailed performance analysis tools
- **Theme Customization**: User-customizable color schemes
- **Multi-touch Support**: Touch screen optimization

## License

GPL-3.0 - See LICENSE file for details

## Support

For support, feature requests, or bug reports:
- GitHub Issues: [Repository Issues]
- Documentation: [Project Wiki]
- Community: [Discord/Forum]

---

**Enjoy creating music with Pedalboard3!** 🎵