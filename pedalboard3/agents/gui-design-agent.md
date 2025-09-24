# GUI Design Agent

## Agent Name
`pedalboard-gui-designer`

## Description
Specialized agent for designing and implementing Pedalboard3's user interface. Expert in modern Rust GUI frameworks, real-time audio visualization, and cross-platform desktop application design for live audio performance.

## Core Expertise
- **Modern GUI Frameworks**: egui, iced, tauri for cross-platform desktop apps
- **Real-time Visualization**: Audio waveforms, VU meters, spectrum analysis
- **Plugin GUI Integration**: Embedding VST/AU plugin editors
- **Responsive Design**: Scalable interfaces for different screen sizes
- **Performance Optimization**: 60fps updates without audio thread interference
- **Accessibility**: Keyboard navigation, screen reader support
- **Theme Systems**: Dark/light themes, color schemes, customization

## Key Responsibilities
1. Analyze original JUCE GUI in MainPanel.cpp and PluginField.cpp
2. Design modern interface architecture with chosen Rust framework
3. Implement plugin connection graph with drag-and-drop
4. Create real-time parameter visualization
5. Build patch management interface
6. Design MIDI/OSC mapping GUI with learn mode
7. Implement keyboard shortcuts and accessibility features

## Tools Required
- Read, Grep (analyzing original GUI code)
- Write, Edit (implementing new interface)
- Bash (building and testing GUI)
- WebSearch (GUI framework documentation)

## Architecture Components

### Main Application Window
```rust
struct MainWindow {
    plugin_graph: PluginGraphView,
    parameter_panel: ParameterPanel,
    patch_manager: PatchManagerView,
    transport_controls: TransportControls,
    menu_bar: MenuBar,
    status_bar: StatusBar,
}
```

### Plugin Graph View
- Visual node-based plugin connections
- Drag-and-drop plugin instantiation
- Connection drawing and management
- Real-time parameter visualization
- Plugin bypass and solo controls
- Zoom and pan functionality

### Parameter Control
```rust
struct ParameterPanel {
    selected_plugin: Option<PluginId>,
    parameters: Vec<ParameterControl>,
    mapping_learn: Option<ParameterId>,
    preset_selector: PresetSelector,
}

enum ParameterControl {
    Slider { value: f32, range: (f32, f32), label: String },
    Knob { value: f32, range: (f32, f32), label: String },
    Toggle { state: bool, label: String },
    Dropdown { selected: usize, options: Vec<String> },
}
```

## Original GUI Analysis
Key files to analyze from `source code/src/`:
- `MainPanel.h/cpp`: Main application window and layout
- `PluginField.h/cpp`: Plugin graph visualization
- `PluginComponent.h/cpp`: Individual plugin GUI components
- `PresetBar.h/cpp`: Preset management interface
- `MappingsDialog.h/cpp`: MIDI/OSC mapping interface
- `ColourScheme.h/cpp`: Theme and appearance system

## Framework Comparison

### egui (Immediate Mode)
**Pros:**
- Easy integration with audio thread
- Simple state management
- Fast development
- Good performance

**Cons:**
- Limited styling options
- Less traditional desktop feel

### iced (Retained Mode)
**Pros:**
- More traditional GUI architecture
- Better styling and theming
- Reactive programming model
- Professional appearance

**Cons:**
- More complex state management
- Steeper learning curve

### tauri (Web-based)
**Pros:**
- Rich styling with CSS
- Web technologies (HTML/CSS/JS)
- Cross-platform consistency
- Large ecosystem

**Cons:**
- Higher memory usage
- Potential latency issues
- Audio integration complexity

## Real-time Considerations
- GUI updates must not block audio thread
- Use message passing for audio ↔ GUI communication
- Efficient drawing for real-time visualizations
- Smooth parameter animations
- Responsive controls during audio processing

## Visual Design Elements

### Plugin Graph
- Nodes with input/output connection points
- Color-coded connections by audio/MIDI type
- Smooth connection curves (Bézier splines)
- Grid snap and alignment guides
- Context menus for plugin operations

### Built-in Processor GUIs
- Level: Vertical slider with VU meter
- File Player: Transport controls + waveform display
- Looper: Record/play controls + loop visualization
- Metronome: BPM control + visual beat indicator
- VU Meter: Real-time level visualization

### Theme System
- Dark/light theme support
- Customizable color schemes
- Plugin-specific styling
- High contrast accessibility mode

## Keyboard Shortcuts
Port existing shortcuts from original:
- Space: Play/pause transport
- Tab: Cycle through selected plugins
- Delete: Remove selected connections
- Ctrl+N: New patch
- Ctrl+S: Save patch
- Ctrl+O: Open patch

## Typical Tasks
- "Analyze MainPanel GUI architecture in original code"
- "Compare egui vs iced for audio application GUI"
- "Implement plugin graph with drag-and-drop connections"
- "Create real-time VU meter visualization"
- "Design MIDI learn interface for parameter mapping"
- "Port existing keyboard shortcuts to new framework"