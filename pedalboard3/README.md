# Pedalboard3 - Modern Audio Plugin Host

**Status:** Phase 1 - Analysis & Planning  
**Project Manager:** pedalboard-project-manager  
**Started:** 2025-09-12  

## Project Overview

Pedalboard3 is a complete modernization of Pedalboard2, migrating from C++/JUCE to Rust while preserving all functionality and improving performance, reliability, and cross-platform support.

### Key Objectives
- **Performance:** Sub-10ms audio latency for real-time use
- **Safety:** Memory-safe audio processing with crash protection  
- **Compatibility:** Support VST/AU/CLAP plugins with sandboxing
- **Control:** Real-time MIDI/OSC parameter mapping and automation
- **Modern UI:** Cross-platform GUI with real-time visualization
- **Backward Compatibility:** Preserve user workflows and file formats

## Architecture

### Crate Structure
- **pedalboard-core** - Main application and coordination
- **pedalboard-audio** - Real-time audio engine  
- **pedalboard-plugins** - Plugin hosting with sandboxing
- **pedalboard-midi** - MIDI/OSC control and automation
- **pedalboard-gui** - Modern cross-platform user interface
- **pedalboard-utils** - Shared utilities and foundational types

### Technical Stack
- **Language:** Rust 2021 (MSRV 1.70)
- **Audio:** cpal-based real-time engine
- **Plugins:** VST2/VST3/AU/CLAP support
- **GUI:** Modern framework (TBD by gui-design agent)
- **Control:** MIDI/OSC with sample-accurate automation
- **Build:** Cargo workspace with optimized profiles

## Development Status

### ✅ Phase 1.1 Complete - Project Structure & Standards
- [x] Cargo workspace configuration
- [x] Crate structure and placeholder implementations
- [x] Development standards and coding guidelines
- [x] Agent coordination framework
- [x] Build system validation

### 🔄 Phase 1.2 In Progress - Architecture Analysis  
- [ ] Original Pedalboard2 codebase analysis
- [ ] Audio engine architecture design
- [ ] Plugin system requirements
- [ ] MIDI/OSC control system analysis
- [ ] GUI framework evaluation
- [ ] Performance requirements specification

### ⏳ Phase 1.3 Planned - Requirements Definition
### ⏳ Phase 1.4 Planned - Risk Assessment
### ⏳ Phase 1.5 Planned - Implementation Planning

## Specialized Agent Team

### 🎵 Audio Architecture Agent (`pedalboard-audio-architect`)
**Responsibility:** Real-time audio processing, FilterGraph analysis, performance optimization

### 🔌 Plugin System Agent (`pedalboard-plugin-system`) 
**Responsibility:** VST/AU/CLAP hosting, sandboxing, parameter management

### 🎛️ MIDI/OSC Mapping Agent (`pedalboard-midi-osc-mapper`)
**Responsibility:** Real-time control, parameter automation, MIDI learn functionality

### 🖥️ GUI Design Agent (`pedalboard-gui-designer`)
**Responsibility:** Modern UI framework selection, component design, visualization

### 🦀 Rust Development Agent (`pedalboard-rust-dev`)
**Responsibility:** Rust implementation, crate integration, build system

### 🧪 Compatibility Testing Agent (`pedalboard-compatibility-tester`)  
**Responsibility:** Migration validation, compatibility testing, performance benchmarking

## Quick Start

### Development Setup
```bash
# Clone and build
cd pedalboard3/
cargo check              # Verify compilation
cargo test               # Run tests (when available)
cargo bench              # Run benchmarks (when available)

# Development workflow  
cargo check --all        # Check all crates
cargo clippy --all       # Lint all crates
cargo fmt --all          # Format all crates
```

### Agent Coordination
See [AGENT_ASSIGNMENTS.md](./AGENT_ASSIGNMENTS.md) for current task assignments and [PROJECT_COORDINATION.md](./PROJECT_COORDINATION.md) for detailed coordination framework.

## Documentation

- **[Development Standards](./DEVELOPMENT_STANDARDS.md)** - Coding standards and architecture principles
- **[Project Coordination](./PROJECT_COORDINATION.md)** - Phase planning and agent coordination
- **[Agent Assignments](./AGENT_ASSIGNMENTS.md)** - Current task assignments and priorities
- **[Agent Definitions](./agents/)** - Specialized agent roles and expertise

## Original Pedalboard2 Reference

The original Pedalboard2 codebase is preserved in the `source code/` directory for analysis and reference. Key components include:

- **FilterGraph** - Audio processing pipeline
- **Plugin System** - VST/AU plugin hosting  
- **MIDI/OSC** - Real-time control mapping
- **GUI Components** - JUCE-based user interface
- **Built-in Processors** - Audio effects and utilities

## Project Status

**Current Phase:** Phase 1.2 - Architecture Analysis  
**Next Milestone:** Complete original codebase analysis (2025-09-16)  
**Project Timeline:** 4-6 weeks for full implementation  
**Risk Level:** Moderate (real-time audio complexity)

---

**Last Updated:** 2025-09-12 by pedalboard-project-manager  
**Next Review:** Daily during active development