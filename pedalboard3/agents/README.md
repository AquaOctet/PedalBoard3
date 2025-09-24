# Pedalboard3 Specialized Agents

This directory contains specialized agent definitions for the Pedalboard modernization project. Each agent is an expert in specific aspects of audio software development and modernization.

## Agent Overview

### 👔 [Project Manager Agent](project-manager-agent.md) ⭐ **PRIMARY SUPERVISOR**
**Name:** `pedalboard-project-manager`  
**Focus:** Project coordination, architecture oversight, risk management, agent supervision
**Key Tasks:** Milestone planning, agent coordination, quality assurance, technical decisions

### 🎵 [Audio Architecture Agent](audio-architecture-agent.md)
**Name:** `pedalboard-audio-architect`
**Focus:** Real-time audio processing, plugin hosting, low-latency optimization
**Key Tasks:** FilterGraph analysis, audio engine design, performance optimization

### 🦀 [Rust Development Agent](rust-development-agent.md) 
**Name:** `pedalboard-rust-dev`
**Focus:** Rust implementation, audio crates, cross-platform development
**Key Tasks:** Cargo workspace setup, cpal integration, memory-safe audio processing

### 🔌 [Plugin System Agent](plugin-system-agent.md)
**Name:** `pedalboard-plugin-system`
**Focus:** VST/AU/CLAP hosting, sandboxing, crash protection
**Key Tasks:** Plugin loading, parameter management, built-in processor porting

### 🎛️ [MIDI/OSC Mapping Agent](midi-osc-mapping-agent.md)
**Name:** `pedalboard-midi-osc-mapper`
**Focus:** Real-time control, parameter mapping, automation
**Key Tasks:** MIDI/OSC input handling, parameter learning, sample-accurate automation

### 🖥️ [GUI Design Agent](gui-design-agent.md)
**Name:** `pedalboard-gui-designer`
**Focus:** Modern UI frameworks, real-time visualization, cross-platform GUIs
**Key Tasks:** Framework selection, plugin graph interface, real-time controls

### 🧪 [Compatibility Testing Agent](compatibility-testing-agent.md)
**Name:** `pedalboard-compatibility-tester`
**Focus:** Migration testing, compatibility validation, performance benchmarking
**Key Tasks:** Audio accuracy tests, plugin compatibility, file format migration

## Usage with Claude Code

To use these agents with the Task tool, reference them by name:

```
Use the pedalboard-project-manager agent to coordinate the modernization effort
Use the pedalboard-audio-architect agent to analyze the FilterGraph system
Use the pedalboard-rust-dev agent to set up the Cargo workspace  
Use the pedalboard-plugin-system agent to design plugin sandboxing
```

**⚠️ Important**: Always start with the **Project Manager Agent** for complex tasks or when coordinating multiple aspects of the modernization. It will direct and coordinate the specialized agents as needed.

## Agent Interaction Patterns

### Hierarchical Workflow
1. **Project Manager Agent** → Coordinate overall modernization effort
   - Directs: **Audio Architecture Agent** → Analyze original system architecture
   - Directs: **Rust Development Agent** → Set up modern Rust project structure
   - Directs: **Plugin System Agent** → Implement plugin hosting system
   - Directs: **MIDI/OSC Mapping Agent** → Build control systems
   - Directs: **GUI Design Agent** → Create user interface
   - Directs: **Compatibility Testing Agent** → Validate migration success

### Parallel Development (Coordinated by Project Manager)
- **Audio + Plugin Agents** → Core audio processing systems
- **MIDI/OSC + GUI Agents** → User interaction systems  
- **Rust + Testing Agents** → Development infrastructure

### Cross-cutting Concerns (Enforced by Project Manager)
- All agents coordinate on **performance requirements** (sub-10ms latency)
- All agents ensure **memory safety** in real-time contexts
- All agents maintain **compatibility** with existing workflows
- **Project Manager** ensures consistency across all components

## Project Structure

Each agent works within this overall project structure:

```
pedalboard3/
├── agents/                    # This directory
├── Cargo.toml                # Workspace configuration
├── crates/
│   ├── pedalboard-core/      # Main application (Rust Dev Agent)
│   ├── pedalboard-audio/     # Audio engine (Audio Architect)
│   ├── pedalboard-plugins/   # Plugin hosting (Plugin System Agent)
│   ├── pedalboard-midi/      # MIDI/OSC (MIDI/OSC Agent)  
│   ├── pedalboard-gui/       # User interface (GUI Agent)
│   └── pedalboard-utils/     # Shared utilities
├── tests/                    # Integration tests (Testing Agent)
├── benches/                  # Performance benchmarks (Testing Agent)
├── docs/                     # Technical documentation
└── examples/                 # Usage examples and demos
```

## Agent Communication

Agents communicate through:
- **Shared interfaces** defined in `pedalboard-utils`
- **Message passing** for real-time audio/GUI communication
- **Configuration files** for cross-cutting concerns
- **Documentation standards** for API consistency

## Development Phases

### Phase 1: Analysis & Planning (Project Manager Coordinated)
- **Project Manager** defines requirements and coordinates analysis
- **Audio Architecture Agent** analyzes original system
- All agents contribute to architecture decisions under PM guidance
- **Testing Agent** defines validation criteria

### Phase 2: Core Implementation (Project Manager Supervised)
- **Project Manager** coordinates implementation priorities
- **Rust Development Agent** sets up project structure
- **Audio Architecture Agent** implements core audio engine
- **Plugin System Agent** creates plugin hosting

### Phase 3: User Interface (Project Manager Integrated)
- **Project Manager** ensures UI/audio integration requirements
- **MIDI/OSC Agent** implements control systems
- **GUI Agent** creates user interface
- **Project Manager** validates integration between control and audio systems

### Phase 4: Validation & Polish (Project Manager Quality Assured)
- **Project Manager** coordinates final integration testing
- **Testing Agent** validates all functionality under PM oversight
- All agents contribute to performance optimization
- **Project Manager** ensures documentation and release readiness

## Contributing New Agents

To add specialized agents:
1. Create agent definition file in this directory
2. Follow the established template structure
3. Define clear expertise boundaries
4. Specify interaction patterns with existing agents
5. Update this README with agent information