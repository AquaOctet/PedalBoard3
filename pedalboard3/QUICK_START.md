# Pedalboard3 - Quick Start Guide

## 🚀 Getting Started

Pedalboard3 is a modern, extensible audio plugin host built in Rust. This implementation uses a **mock audio backend** that requires no system audio dependencies, making it perfect for development and testing.

### Prerequisites

- Rust 1.70+ (install from https://rustup.rs/)
- No additional system dependencies required!

### Build and Run

1. **Build the project:**
   ```bash
   cargo build
   ```

2. **Run the quick demo:**
   ```bash
   ./demo.sh
   ```

3. **Or try individual features:**
   ```bash
   # System validation
   cargo run --bin pedalboard3 -- --validate
   
   # List audio devices (mock devices)
   cargo run --bin pedalboard3 -- --list-devices
   
   # Scan for plugins
   cargo run --bin pedalboard3 -- --scan-plugins
   
   # Run in headless mode
   cargo run --bin pedalboard3 -- --headless -v
   
   # See all options
   cargo run --bin pedalboard3 -- --help
   ```

## 🏗️ Architecture Overview

### Current Implementation Status

✅ **Working Features:**
- Modular Rust workspace architecture
- Mock audio engine (no system dependencies)
- Plugin scanning system 
- Configuration management
- Event-driven application architecture
- Comprehensive logging and tracing
- Command-line interface
- Headless operation mode

🚧 **Ready for Extension:**
- VST2/VST3 plugin support (framework in place)
- GUI integration (architecture ready)
- Real audio I/O (can be enabled with cpal features)
- MIDI and OSC support (framework exists)
- Session and preset management

### Project Structure

```
pedalboard3/
├── crates/
│   ├── pedalboard-core/     # Main application logic
│   ├── pedalboard-audio/    # Audio engine (mock + real)
│   ├── pedalboard-plugins/  # Plugin hosting system
│   ├── pedalboard-midi/     # MIDI handling
│   ├── pedalboard-gui/      # GUI framework
│   └── pedalboard-utils/    # Shared utilities
├── examples/                # Usage examples
├── docs/                    # Documentation
├── tests/                   # Integration tests
└── demo.sh                  # Quick demonstration
```

## 🔧 Development

### Adding Real Audio Support

To enable real audio hardware support:

1. Install system dependencies:
   ```bash
   # On Ubuntu/Debian
   sudo apt install libasound2-dev pkg-config
   
   # On macOS (with Homebrew)
   brew install pkg-config
   ```

2. Enable audio features:
   ```bash
   cargo run --features audio --bin pedalboard3
   ```

### Extending the System

The architecture is designed for easy extension:

- **Add new plugin formats:** Implement the `PluginInstance` trait
- **Add GUI frameworks:** Use the existing event system  
- **Add audio backends:** Implement alternative audio engines
- **Add MIDI controllers:** Extend the MIDI handling system

## 🧪 Testing

Run the test suite:
```bash
cargo test
```

Run with verbose output:
```bash
cargo test -- --nocapture
```

## 🎯 Key Features Demonstrated

1. **Zero System Dependencies:** Runs anywhere Rust compiles
2. **Plugin Architecture:** Extensible plugin system ready for VST/AU/LV2
3. **Event-Driven:** Reactive architecture for real-time audio
4. **Configuration Management:** Flexible, persistent settings
5. **Production Ready:** Proper error handling, logging, and validation

## 📝 Next Steps

This demo shows a fully functional foundation that can be extended with:
- Real audio I/O for live performance
- VST/AU plugin integration 
- GUI development with egui/iced/tauri
- Advanced routing and signal processing
- Session management and automation

The architecture is designed to support these features without major refactoring!