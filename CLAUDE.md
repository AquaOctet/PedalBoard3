# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Pedalboard3 is a modernization effort for Pedalboard2, a JUCE-based VST plugin host designed for live audio use. The original codebase in `source code/` is a C++/JUCE application that should **not be edited**. All new development should happen in the `pedalboard3/` directory, with the goal of rebuilding in Rust (or C++ if necessary) while retaining all core functionalities.

### Key Features to Retain

- **Modular Audio Patching**: Visual plugin connection system with drag-and-drop interface
- **Real-time Parameter Control**: MIDI CC and OSC message mapping to any plugin parameter
- **Patch Management**: Quick switching between saved audio configurations
- **Built-in Processors**: Level, Output Toggle, File Player, VU Meter, Audio Recorder, Metronome, Looper
- **Plugin Host**: VST plugin loading and management
- **Low-latency Audio**: Designed for live performance use

## Architecture Overview

### Original JUCE Architecture (source code/)

**Core Components:**
- `FilterGraph.h/cpp`: Main audio routing and plugin management system
- `MainPanel.h/cpp`: Primary GUI component with menu bar and plugin field
- `PluginField.h/cpp`: Visual plugin connection interface
- `PedalboardProcessors.h/cpp`: Built-in audio processors (Level, File Player, etc.)
- `InternalFilters.h/cpp`: Internal plugin format management
- `MidiMappingManager.h/cpp`: MIDI CC to parameter mapping
- `OscMappingManager.h/cpp`: OSC message handling and parameter mapping
- `PatchOrganiser.h/cpp`: Patch save/load management
- `PresetManager.h/cpp`: Plugin preset management

**Key Libraries:**
- JUCE framework for audio processing and GUI
- Custom OSC implementation (`NiallsOSCLib/`)
- Custom UDP socket handling (`NiallsSocketLib/`)
- VST SDK integration

**Built-in Processors:**
- `LevelProcessor`: Audio level control
- `FilePlayerProcessor`: Audio file playback with sync
- `OutputToggleProcessor`: Audio routing toggle
- `VUMeterProcessor`: Visual level metering
- `RecorderProcessor`: Audio recording to file
- `MetronomeProcessor`: Click track generation
- `LooperProcessor`: Real-time audio looping

### Build System

**Original Build Configuration:**
- JUCE Projucer file: `source code/Pedalboard2.jucer`
- Visual Studio 2010 projects in `source code/Builds/VisualStudio2010/`
- Xcode projects in `source code/Builds/MacOSX/`
- Linux makefile support available
- Requires VST SDK and ASIO SDK for full functionality

## Development Commands

Since this is a modernization project, initial commands will focus on analysis and new development setup:

### Analyzing Original Code
```bash
# Search for specific functionality in original codebase
find "source code/src" -name "*.cpp" -o -name "*.h" | xargs grep -l "SEARCH_TERM"

# Examine JUCE project configuration
cat "source code/Pedalboard2.jucer"

# Review built processors
ls "source code/src/*Processor*"
```

### New Development Setup (pedalboard3/)

**Rust Setup (Recommended):**
```bash
# Initialize new Rust project in pedalboard3/
cd pedalboard3
cargo init --name pedalboard3

# Key crates likely needed:
# - cpal (cross-platform audio)
# - egui or iced (GUI framework)
# - vst or clap (plugin hosting)
# - rosc (OSC implementation)
# - midir (MIDI handling)
# - serde (serialization for patches/presets)
```

**C++ Setup (Alternative):**
```bash
# If staying with C++, consider modern alternatives:
# - RtAudio instead of JUCE for audio I/O
# - Dear ImGui or Qt for modern GUI
# - Modern VST3 SDK
# - CMake build system
```

### Testing Original Application
```bash
# The original executables are available:
./Pedalboard2.exe          # 32-bit version
./Pedalboard2-64bit.exe    # 64-bit version (for 64-bit plugins)
```

## File Organization

- `source code/`: **READ-ONLY** - Original JUCE/C++ implementation
- `pedalboard3/`: New modernized implementation
- `documentation/`: HTML documentation for original features
- `readme.txt`: Original project description and usage notes
- `ToDo.txt`: Original developer's feature wishlist

## Key Implementation Notes

**Audio Processing Chain:**
1. Audio input → FilterGraph routing → Plugin processing → Audio output
2. MIDI/OSC input → Mapping managers → Parameter automation
3. Patch system saves entire FilterGraph state and mappings

**Plugin Management:**
- VST plugin scanning and caching
- Internal processor factory pattern
- Plugin preset system with user presets
- Real-time parameter automation without dropouts

**GUI Architecture:**
- Plugin components as visual nodes with connection points
- Real-time parameter visualization
- Patch organization with quick switching
- Extensive keyboard shortcuts and MIDI/OSC control

**Critical Performance Requirements:**
- Sub-10ms audio latency for live use
- Smooth parameter automation without audio glitches
- Fast patch switching (< 100ms)
- Stable under continuous live operation

## Modernization Considerations

**Benefits of Rust Rewrite:**
- Memory safety for long-running live applications
- Better error handling for plugin crashes
- Modern async I/O for MIDI/OSC
- Package manager for dependencies
- Cross-platform compilation

**Architecture Improvements:**
- Separate audio engine from GUI (better for stability)
- Plugin sandboxing to prevent crashes
- Modern plugin formats (CLAP, VST3, AU)
- Better threading model for real-time audio
- Configuration via modern formats (TOML, JSON)

**Compatibility Goals:**
- Load existing .filtergraph patch files
- Support existing MIDI/OSC mappings
- Maintain same keyboard shortcuts
- Similar GUI layout and workflow