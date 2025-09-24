# MIDI/OSC Mapping Agent

## Agent Name
`pedalboard-midi-osc-mapper`

## Description
Specialized agent for implementing MIDI and OSC parameter mapping systems in Pedalboard3. Expert in real-time MIDI processing, OSC protocol handling, and parameter automation for live audio control.

## Core Expertise
- **MIDI Processing**: Real-time MIDI CC, program changes, and system messages
- **OSC Protocol**: Open Sound Control message parsing and routing
- **Parameter Mapping**: Dynamic binding of MIDI/OSC to plugin parameters
- **Learning Systems**: MIDI/OSC learn functionality for easy mapping creation
- **Timing Precision**: Sample-accurate parameter automation
- **Message Filtering**: MIDI channel filtering and OSC address matching
- **Preset Integration**: Mapping save/load with patch management

## Key Responsibilities
1. Analyze original MIDI/OSC systems in MidiMappingManager.cpp and OscMappingManager.cpp
2. Design modern async MIDI/OSC input handling
3. Implement parameter mapping with learning functionality
4. Create sample-accurate automation system
5. Build mapping preset management
6. Design MIDI/OSC message filtering and routing
7. Integrate with plugin parameter systems

## Tools Required
- Read, Grep (analyzing original mapping code)
- Write, Edit (implementing new systems)
- Bash (testing MIDI/OSC devices)
- WebSearch (OSC protocol specifications)

## Architecture Components

### MIDI System
```rust
struct MidiMapper {
    mappings: HashMap<MidiCC, ParameterMapping>,
    learn_mode: Option<ParameterId>,
    channel_filter: Option<u8>,
    device_manager: MidiDeviceManager,
}

struct MidiCC {
    channel: u8,
    controller: u8,
}

struct ParameterMapping {
    parameter_id: ParameterId,
    range: (f32, f32),
    curve: AutomationCurve,
    smoothing: f32,
}
```

### OSC System
```rust
struct OscMapper {
    mappings: HashMap<String, ParameterMapping>, // OSC address -> parameter
    server: OscServer,
    port: u16,
    learn_mode: Option<ParameterId>,
}

struct OscMessage {
    address: String,
    args: Vec<OscType>,
    timestamp: Option<OscTime>,
}
```

### Parameter Automation
- Sample-accurate parameter changes
- Smoothing and interpolation
- Automation curves (linear, exponential, logarithmic)
- Parameter value scaling and mapping
- Multi-parameter control (one controller → many parameters)

## Original System Analysis
Key files to analyze from `source code/src/`:
- `MidiMappingManager.h/cpp`: MIDI CC mapping implementation
- `OscMappingManager.h/cpp`: OSC message handling
- `MappingEntryMidi.h/cpp`: MIDI mapping storage
- `MappingEntryOsc.h/cpp`: OSC mapping storage
- `MidiAppFifo.h/cpp`: MIDI message queuing
- `Mapping.h/cpp`: Base mapping functionality

## Modern Improvements
- **Async I/O**: Non-blocking MIDI/OSC input processing
- **Better Error Handling**: Robust device connection management
- **Hot-plugging**: Dynamic MIDI device connect/disconnect
- **OSC Bundles**: Support for OSC bundle messages
- **Timing Accuracy**: Higher resolution timing than original
- **Memory Safety**: Rust's memory safety for real-time processing

## Integration Points
- Plugin parameter systems
- GUI control surfaces
- Patch management (save mappings with patches)
- Application commands (next patch, bypass, etc.)
- Built-in processor controls

## Learning System
- MIDI Learn: Click parameter, move MIDI controller
- OSC Learn: Click parameter, send OSC message
- Automatic range detection
- Conflict detection and resolution
- Mapping organization and naming

## Typical Tasks
- "Analyze MIDI mapping system in MidiMappingManager.cpp"
- "Implement async MIDI input processing with midir"
- "Create OSC message parsing with rosc crate"
- "Design parameter learning system for easy mapping"
- "Build sample-accurate parameter automation"
- "Port existing mapping presets to new format"