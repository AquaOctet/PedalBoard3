# Phase 2 Implementation Specification - Pedalboard3

**Project Manager Coordination Document**  
**Date:** 2025-09-12  
**Phase:** 2.0 Implementation Planning  
**Status:** Ready for Execution

## Executive Summary

Following the successful completion of Phase 1.2 critical architecture analysis, this specification defines the concrete implementation plan for Pedalboard3's core systems. All specialized agents have completed their analysis and this document integrates their findings into actionable development tasks.

## Implementation Architecture

### Core Rust Crate Structure

```
pedalboard3/
├── Cargo.toml                    # Workspace configuration
├── crates/
│   ├── pedalboard-core/          # 🎵 Audio graph and processing engine
│   ├── pedalboard-audio/         # 🔊 Real-time audio I/O and JACK integration  
│   ├── pedalboard-plugins/       # 🔌 Plugin hosting (VST/AU/LADSPA)
│   ├── pedalboard-midi/          # 🎛️ MIDI/OSC mapping and control
│   ├── pedalboard-gui/           # 🖥️ User interface and visualization
│   └── pedalboard-utils/         # 🛠️ Shared utilities and types
├── examples/                     # Example applications and demos
├── tests/                        # Integration tests
└── benches/                      # Performance benchmarks
```

## Phase 2.1: Core Audio Engine Implementation

**Duration:** 2 weeks  
**Lead Agent:** Audio Architecture Agent  
**Dependencies:** None (foundational)

### Task Breakdown

#### Week 1: Audio Graph Foundation
```rust
// pedalboard-core/src/audio_graph.rs
pub struct AudioGraph {
    nodes: HashMap<NodeId, AudioNode>,
    connections: Vec<Connection>,
    processing_order: Vec<NodeId>,
    sample_rate: f64,
    buffer_size: usize,
}

impl AudioGraph {
    pub fn add_node(&mut self, plugin: Box<dyn AudioPlugin>) -> NodeId;
    pub fn remove_node(&mut self, id: NodeId) -> Result<(), GraphError>;
    pub fn connect(&mut self, src: NodeId, dst: NodeId) -> Result<(), GraphError>;
    pub fn process_block(&mut self, inputs: &[&[f32]], outputs: &mut [&mut [f32]]);
}
```

**Key Implementation Requirements:**
- **Real-time Safety**: No allocations in `process_block()`
- **Topological Sorting**: Dependency-ordered processing
- **Cycle Detection**: Prevent feedback loops
- **Buffer Management**: Efficient temporary buffer reuse

#### Week 2: Node Management System
```rust
// pedalboard-core/src/audio_node.rs
pub struct AudioNode {
    id: NodeId,
    plugin: Box<dyn AudioPlugin>,
    inputs: Vec<NodePin>,
    outputs: Vec<NodePin>,
    bypass: AtomicBool,
    position: AtomicCell<(f32, f32)>,  // For GUI
}

pub struct Connection {
    source_node: NodeId,
    source_pin: usize,
    dest_node: NodeId,
    dest_pin: usize,
}
```

**Critical Features:**
- **Thread-safe State**: Atomic operations for bypass/position
- **Dynamic Routing**: Runtime connection changes
- **Buffer Optimization**: Minimize memory copies
- **Error Recovery**: Graceful handling of plugin failures

#### Deliverables
- ✅ Core audio graph processing engine
- ✅ Node management with thread-safe state
- ✅ Connection routing system
- ✅ Real-time performance validation
- ✅ Unit tests for all core functionality

## Phase 2.2: Real-time Audio I/O Integration

**Duration:** 1.5 weeks  
**Lead Agent:** Audio Architecture Agent  
**Dependencies:** Phase 2.1 (Core Engine)

### JACK Client Implementation
```rust
// pedalboard-audio/src/jack_client.rs
pub struct JackClient {
    client: jack::Client,
    audio_graph: Arc<Mutex<AudioGraph>>,
    input_ports: Vec<jack::Port<jack::AudioIn>>,
    output_ports: Vec<jack::Port<jack::AudioOut>>,
    midi_input: jack::Port<jack::MidiIn>,
    parameter_queue: crossbeam::channel::Receiver<ParameterUpdate>,
}

impl JackClient {
    pub fn new(name: &str, num_inputs: usize, num_outputs: usize) -> Result<Self, AudioError>;
    
    fn process_callback(&mut self, nframes: jack::Frames) -> jack::Control {
        // Real-time audio processing - ALLOCATION FREE
        let inputs = self.collect_input_buffers(nframes);
        let mut outputs = self.prepare_output_buffers(nframes);
        
        // Process parameter updates from GUI thread
        while let Ok(update) = self.parameter_queue.try_recv() {
            self.apply_parameter_update(update);
        }
        
        // Main audio processing
        if let Ok(mut graph) = self.audio_graph.try_lock() {
            graph.process_block(&inputs, &mut outputs);
        } else {
            // Graph busy - pass audio through silently
            self.pass_through(&inputs, &mut outputs);
        }
        
        jack::Control::Continue
    }
}
```

### Performance Requirements
- **Latency Target**: <10ms round-trip
- **Real-time Compliance**: No blocking operations in audio thread
- **CPU Usage**: <50% at 44.1kHz/512 samples
- **Memory**: Zero allocation during processing

#### Deliverables
- ✅ JACK audio client with real-time processing
- ✅ Thread-safe parameter communication
- ✅ Performance benchmarks meeting targets
- ✅ Cross-platform audio driver abstraction

## Phase 2.3: Plugin Hosting System

**Duration:** 2 weeks  
**Lead Agent:** Plugin System Agent  
**Dependencies:** Phase 2.1 (Core Engine)

### Week 1: VST Plugin Support
```rust
// pedalboard-plugins/src/vst_wrapper.rs
pub struct VstWrapper {
    plugin: Box<dyn vst::plugin::Plugin>,
    info: vst::plugin::Info,
    parameter_cache: Vec<AtomicU32>, // f32 as u32 bits for atomic access
    bypass_buffer: Vec<Vec<f32>>,
    bypass_ramp: f32,
}

impl AudioPlugin for VstWrapper {
    fn process_block(
        &mut self,
        inputs: &[&[f32]],
        outputs: &mut [&mut [f32]],
        midi: &mut MidiBuffer,
    ) -> Result<(), PluginError> {
        // Apply parameter updates
        self.update_parameters();
        
        // VST processing
        let mut buffer = vst::buffer::AudioBuffer::new(inputs, outputs);
        self.plugin.process(&mut buffer);
        
        // Apply bypass if needed
        if self.is_bypassed() {
            self.apply_bypass_ramping(inputs, outputs)?;
        }
        
        Ok(())
    }
}
```

### Week 2: Multi-format Support
```rust
// pedalboard-plugins/src/plugin_scanner.rs
pub struct PluginScanner {
    vst_paths: Vec<PathBuf>,
    au_paths: Vec<PathBuf>,
    ladspa_paths: Vec<PathBuf>,
    scan_cache: HashMap<PathBuf, PluginInfo>,
}

impl PluginScanner {
    pub fn scan_all_formats(&mut self) -> Vec<PluginInfo> {
        let mut plugins = Vec::new();
        plugins.extend(self.scan_vst_plugins());
        
        #[cfg(target_os = "macos")]
        plugins.extend(self.scan_au_plugins());
        
        #[cfg(unix)]
        plugins.extend(self.scan_ladspa_plugins());
        
        plugins
    }
}
```

#### Deliverables
- ✅ VST 2.4 plugin hosting with bypass system
- ✅ LADSPA plugin support (Linux)
- ✅ Audio Units support (macOS)
- ✅ Plugin scanner and management
- ✅ Crash recovery and sandboxing

## Phase 2.4: GUI System Development

**Duration:** 2 weeks  
**Lead Agent:** GUI Design Agent  
**Dependencies:** Phase 2.1, 2.3 (Core + Plugins)

### GUI Framework Selection: `egui`
- **Pros**: Immediate mode, Rust-native, good performance
- **Cons**: Limited widget ecosystem vs traditional retained-mode GUIs
- **Decision**: Optimal for audio applications requiring fast updates

### Main Application Window
```rust
// pedalboard-gui/src/main_window.rs
pub struct MainWindow {
    audio_graph: Arc<Mutex<AudioGraph>>,
    plugin_components: HashMap<NodeId, PluginComponent>,
    available_plugins: Vec<PluginInfo>,
    parameter_sender: crossbeam::channel::Sender<ParameterUpdate>,
    
    // UI State
    selected_plugin: Option<NodeId>,
    dragging_plugin: Option<(NodeId, Vec2)>,
    zoom_level: f32,
    pan_offset: Vec2,
}

impl MainWindow {
    pub fn ui(&mut self, ctx: &egui::Context) {
        egui::CentralPanel::default().show(ctx, |ui| {
            // Plugin canvas with zoom/pan
            let canvas_response = ui.allocate_response(ui.available_size(), egui::Sense::drag());
            
            // Draw connections
            self.draw_connections(ui);
            
            // Draw plugin components
            for (id, component) in &mut self.plugin_components {
                component.ui(ui, ctx);
                
                // Handle plugin interactions
                if component.response.double_clicked() {
                    self.open_plugin_editor(*id);
                }
            }
            
            // Handle canvas interactions
            self.handle_canvas_input(&canvas_response);
        });
        
        // Plugin browser sidebar
        self.plugin_browser_ui(ctx);
        
        // Transport controls
        self.transport_ui(ctx);
    }
}
```

### Plugin Component Visualization
```rust
// pedalboard-gui/src/plugin_component.rs
pub struct PluginComponent {
    id: NodeId,
    name: String,
    position: Vec2,
    size: Vec2,
    pins: Vec<PluginPin>,
    bypass_state: bool,
    response: egui::Response,
}

impl PluginComponent {
    pub fn ui(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) -> egui::Response {
        let rect = egui::Rect::from_min_size(self.position, self.size);
        let response = ui.allocate_rect(rect, egui::Sense::click_and_drag());
        
        // Draw plugin background with rounded corners
        ui.painter().rect_filled(
            rect,
            egui::Rounding::same(8.0),
            if self.bypass_state {
                egui::Color32::from_rgb(100, 100, 100)
            } else {
                egui::Color32::from_rgb(60, 60, 80)
            }
        );
        
        // Plugin name
        ui.painter().text(
            rect.center_top() + egui::vec2(0.0, 8.0),
            egui::Align2::CENTER_TOP,
            &self.name,
            egui::FontId::proportional(12.0),
            egui::Color32::WHITE,
        );
        
        // Input/output pins
        self.draw_pins(ui, rect);
        
        // Control buttons (edit, bypass, delete)
        self.draw_controls(ui, rect);
        
        self.response = response;
        response
    }
}
```

#### Deliverables
- ✅ Main application window with plugin canvas
- ✅ Plugin component visualization and interaction
- ✅ Plugin browser and loading interface
- ✅ Real-time parameter control interface
- ✅ Connection visualization and editing

## Phase 2.5: MIDI/OSC Control Integration

**Duration:** 1.5 weeks  
**Lead Agent:** MIDI/OSC Mapping Agent  
**Dependencies:** Phase 2.1, 2.4 (Core + GUI)

### Real-time Parameter Mapping
```rust
// pedalboard-midi/src/mapping_manager.rs
pub struct MappingManager {
    midi_mappings: HashMap<MidiCC, Vec<ParameterMapping>>,
    osc_mappings: HashMap<String, Vec<ParameterMapping>>,
    learn_mode: Option<LearnSession>,
    parameter_sender: crossbeam::channel::Sender<ParameterUpdate>,
}

pub struct ParameterMapping {
    plugin_id: NodeId,
    parameter_index: u32,
    range: (f32, f32),
    curve: MappingCurve,
    latched: bool,
}

impl MappingManager {
    pub fn handle_midi_cc(&mut self, cc: MidiCC, value: u8) {
        if let Some(mappings) = self.midi_mappings.get(&cc) {
            let normalized_value = value as f32 / 127.0;
            
            for mapping in mappings {
                let mapped_value = mapping.apply_curve(normalized_value);
                let final_value = mapping.range.0 + 
                    (mapping.range.1 - mapping.range.0) * mapped_value;
                
                // Send to audio thread via lock-free channel
                let update = ParameterUpdate {
                    plugin_id: mapping.plugin_id,
                    parameter: mapping.parameter_index,
                    value: final_value,
                };
                
                let _ = self.parameter_sender.try_send(update);
            }
        }
    }
}
```

#### Deliverables
- ✅ MIDI CC mapping system with learning mode
- ✅ OSC message handling and routing
- ✅ Real-time parameter automation
- ✅ Mapping persistence and presets
- ✅ Sample-accurate timing support

## Phase 2.6: Integration and Testing

**Duration:** 1 week  
**Lead Agent:** Compatibility Testing Agent  
**Dependencies:** All previous phases

### Integration Testing Strategy
```rust
// tests/integration_tests.rs
#[test]
fn test_end_to_end_audio_processing() {
    let mut audio_graph = AudioGraph::new(44100.0, 512);
    
    // Add test plugin chain
    let reverb_id = audio_graph.add_plugin(Box::new(TestReverbPlugin::new()));
    let delay_id = audio_graph.add_plugin(Box::new(TestDelayPlugin::new()));
    
    // Connect plugins
    audio_graph.connect(reverb_id, delay_id).unwrap();
    
    // Process test signal
    let input = generate_sine_wave(440.0, 512);
    let mut output = vec![vec![0.0; 512]; 2];
    
    audio_graph.process_block(&[&input], &mut [&mut output[0], &mut output[1]]);
    
    // Verify output characteristics
    assert!(output_has_expected_properties(&output));
}

#[test]
fn test_real_time_performance() {
    let mut audio_graph = AudioGraph::new(44100.0, 512);
    load_complex_plugin_chain(&mut audio_graph);
    
    let start = Instant::now();
    for _ in 0..1000 {
        process_audio_block(&mut audio_graph);
    }
    let duration = start.elapsed();
    
    // Must complete 1000 blocks faster than real-time equivalent
    let real_time_duration = Duration::from_millis(1000 * 512 * 1000 / 44100);
    assert!(duration < real_time_duration);
}
```

### Performance Benchmarks
- **Latency Measurements**: Round-trip audio latency <10ms
- **CPU Usage**: Plugin processing overhead <2x original
- **Memory Usage**: No runtime allocations in audio thread
- **Throughput**: Handle 50+ simultaneous plugins at 44.1kHz

#### Deliverables
- ✅ Comprehensive integration test suite
- ✅ Performance benchmarks and validation
- ✅ Real-world plugin compatibility testing
- ✅ Stress testing under high CPU load
- ✅ Memory leak detection and prevention

## Success Criteria

### Technical Milestones
1. **Audio Quality**: Bit-perfect processing compared to Pedalboard2
2. **Performance**: Equal or better latency and CPU efficiency
3. **Compatibility**: Support for 95%+ of tested VST plugins
4. **Stability**: 24+ hour continuous operation without crashes
5. **Usability**: Feature parity with original JUCE-based interface

### Risk Mitigation Strategies
1. **Real-time Performance**: Continuous benchmarking and optimization
2. **Plugin Compatibility**: Extensive testing with popular plugin libraries
3. **Memory Safety**: Comprehensive testing with Valgrind/ASan
4. **Cross-platform**: CI testing on Linux, macOS, and Windows
5. **User Experience**: Iterative design validation with target users

## Phase 2 Timeline Summary

```
Week 1-2:   Core Audio Engine (Phase 2.1)
Week 3:     JACK Integration (Phase 2.2) 
Week 4-5:   Plugin Hosting (Phase 2.3)
Week 6-7:   GUI System (Phase 2.4)
Week 8:     MIDI/OSC Control (Phase 2.5)
Week 9:     Integration/Testing (Phase 2.6)
```

**Total Duration:** 9 weeks  
**Resource Requirements:** 1-2 Rust developers with audio programming experience  
**Critical Dependencies:** JACK development libraries, VST SDK access

## Post-Phase 2 Roadmap

### Phase 3: Advanced Features (Weeks 10-12)
- Plugin preset management system
- Advanced MIDI/OSC mapping features  
- Multi-track/multi-patch support
- Performance optimization and profiling

### Phase 4: Production Release (Weeks 13-16)
- Documentation and user guides
- Installer packages for major platforms
- Beta testing with existing Pedalboard2 users
- Migration tools for existing patches

---

**Project Manager Status:** Phase 2 implementation specification complete. All specialized agents have provided technical input and architectural guidance. Ready to commence development with well-defined milestones and success criteria.