# Plugin System Migration Strategy - Pedalboard2 to Rust

**Date:** 2025-09-12  
**Agent:** Plugin System Agent  
**Phase:** 1.2 Technical Analysis - Migration Planning

## Overview

This document outlines the comprehensive strategy for migrating Pedalboard2's sophisticated plugin hosting system to Rust, maintaining full compatibility with VST, AU, LADSPA, and internal plugins while leveraging Rust's safety guarantees.

## Current Plugin System Architecture

### BypassableInstance Wrapper System

The original system uses a sophisticated wrapper architecture:

```cpp
class BypassableInstance : public AudioPluginInstance {
private:
    AudioPluginInstance *plugin;          // Wrapped plugin
    AudioSampleBuffer tempBuffer;         // For bypass functionality
    bool bypass;                          // Bypass state
    float bypassRamp;                     // Smooth bypass ramping
    int midiChannel;                      // MIDI channel filtering
    MidiMessageCollector midiCollector;   // OSC-to-MIDI conversion
    
public:
    void processBlock(AudioSampleBuffer &buffer, MidiBuffer &midiMessages) override;
    void setBypass(bool val);
    void addMidiMessage(const MidiMessage& message);  // From OSC
};
```

### Plugin Component GUI System

Visual plugin representation with full interaction:

```cpp
class PluginComponent : public Component, public ChangeBroadcaster {
private:
    AudioProcessorGraph::Node *node;      // Audio graph node
    TextButton *editButton;               // Open plugin editor
    TextButton *mappingsButton;           // Parameter mappings
    DrawableButton *bypassButton;         // Bypass control
    DrawableButton *deleteButton;         // Remove plugin
    PluginEditorWindow *pluginWindow;     // Plugin's custom editor
    
public:
    void paint(Graphics& g) override;     // Custom plugin rendering
    void mouseDown(const MouseEvent& e) override;  // Drag and drop
    void buttonClicked(Button *button) override;   // UI interaction
};
```

## Rust Migration Architecture

### Core Plugin Wrapper Design

```rust
use std::sync::Arc;
use crossbeam::channel::{Receiver, Sender};

pub struct PluginWrapper {
    plugin: Box<dyn AudioPlugin + Send + Sync>,
    bypass_buffer: Vec<Vec<f32>>,
    bypass_state: Arc<AtomicBool>,
    bypass_ramp: f32,
    midi_channel: u8,
    parameter_queue: Receiver<ParameterUpdate>,
    state: PluginState,
}

impl PluginWrapper {
    pub fn new(plugin: Box<dyn AudioPlugin + Send + Sync>) -> Self {
        let num_channels = plugin.num_inputs().max(plugin.num_outputs()) as usize;
        Self {
            plugin,
            bypass_buffer: vec![vec![0.0; 4096]; num_channels], // Pre-allocated
            bypass_state: Arc::new(AtomicBool::new(false)),
            bypass_ramp: 0.0,
            midi_channel: 0, // 0 = omni
            parameter_queue: crossbeam::channel::unbounded().1,
            state: PluginState::default(),
        }
    }
    
    pub fn process_block(
        &mut self, 
        inputs: &[&[f32]], 
        outputs: &mut [&mut [f32]], 
        midi_events: &mut MidiBuffer
    ) -> Result<(), PluginError> {
        // Filter MIDI by channel if needed
        if self.midi_channel > 0 {
            midi_events.retain(|event| {
                event.channel() == self.midi_channel || event.channel() == 0
            });
        }
        
        // Apply parameter updates from UI thread
        while let Ok(param_update) = self.parameter_queue.try_recv() {
            self.plugin.set_parameter(param_update.index, param_update.value)?;
        }
        
        let bypass = self.bypass_state.load(Ordering::Relaxed);
        
        if !bypass {
            // Normal processing
            self.plugin.process_block(inputs, outputs, midi_events)?;
        } else {
            // Bypass with smooth ramping
            self.process_bypass(inputs, outputs)?;
        }
        
        Ok(())
    }
    
    fn process_bypass(
        &mut self, 
        inputs: &[&[f32]], 
        outputs: &mut [&mut [f32]]
    ) -> Result<(), PluginError> {
        let block_size = inputs[0].len();
        
        // Copy input to temporary buffer
        for (ch, input) in inputs.iter().enumerate() {
            if ch < self.bypass_buffer.len() {
                self.bypass_buffer[ch][..block_size].copy_from_slice(input);
            }
        }
        
        // Process plugin into outputs
        self.plugin.process_block(inputs, outputs, &mut MidiBuffer::new())?;
        
        // Apply bypass ramping
        for (ch, output) in outputs.iter_mut().enumerate() {
            if ch < self.bypass_buffer.len() {
                let orig_data = &self.bypass_buffer[ch][..block_size];
                let mut ramp_val = self.bypass_ramp;
                
                for (i, sample) in output.iter_mut().enumerate() {
                    *sample = (orig_data[i] * ramp_val) + (*sample * (1.0 - ramp_val));
                    
                    // Smooth ramp to bypass
                    if ramp_val < 1.0 {
                        ramp_val += 0.001; // ~1 second ramp at 44.1kHz
                        if ramp_val > 1.0 { ramp_val = 1.0; }
                    }
                }
                
                self.bypass_ramp = ramp_val;
            }
        }
        
        Ok(())
    }
}
```

### Plugin Host Trait System

```rust
pub trait AudioPlugin: Send + Sync {
    fn name(&self) -> &str;
    fn vendor(&self) -> &str;
    fn num_inputs(&self) -> u32;
    fn num_outputs(&self) -> u32;
    fn num_parameters(&self) -> u32;
    
    fn parameter_name(&self, index: u32) -> String;
    fn parameter_value(&self, index: u32) -> f32;
    fn set_parameter(&mut self, index: u32, value: f32) -> Result<(), PluginError>;
    
    fn prepare_to_play(&mut self, sample_rate: f64, max_block_size: u32) -> Result<(), PluginError>;
    fn process_block(
        &mut self, 
        inputs: &[&[f32]], 
        outputs: &mut [&mut [f32]], 
        midi: &mut MidiBuffer
    ) -> Result<(), PluginError>;
    
    fn save_state(&self) -> Result<Vec<u8>, PluginError>;
    fn load_state(&mut self, state: &[u8]) -> Result<(), PluginError>;
    
    fn create_editor(&self) -> Option<Box<dyn PluginEditor>>;
}

pub trait PluginEditor: Send {
    fn show(&mut self) -> Result<(), PluginError>;
    fn hide(&mut self) -> Result<(), PluginError>;
    fn size(&self) -> (u32, u32);
    fn resize(&mut self, width: u32, height: u32) -> Result<(), PluginError>;
}
```

### VST Plugin Implementation

```rust
use vst::plugin::{Plugin, Info};
use vst::buffer::AudioBuffer;

pub struct VstWrapper {
    plugin: Box<dyn Plugin>,
    info: Info,
    sample_rate: f64,
}

impl VstWrapper {
    pub fn load_from_path(path: &Path) -> Result<Self, PluginError> {
        // Use vst crate to load VST plugin
        let host = vst::host::Host::default();
        let plugin = vst::loader::load_plugin(path, host)
            .map_err(|e| PluginError::LoadFailed(e.to_string()))?;
        
        let info = plugin.get_info();
        
        Ok(Self {
            plugin,
            info,
            sample_rate: 44100.0,
        })
    }
}

impl AudioPlugin for VstWrapper {
    fn name(&self) -> &str { &self.info.name }
    fn vendor(&self) -> &str { &self.info.vendor }
    fn num_inputs(&self) -> u32 { self.info.inputs as u32 }
    fn num_outputs(&self) -> u32 { self.info.outputs as u32 }
    fn num_parameters(&self) -> u32 { self.info.parameters as u32 }
    
    fn process_block(
        &mut self, 
        inputs: &[&[f32]], 
        outputs: &mut [&mut [f32]], 
        _midi: &mut MidiBuffer
    ) -> Result<(), PluginError> {
        let mut buffer = AudioBuffer::new(inputs, outputs);
        self.plugin.process(&mut buffer);
        Ok(())
    }
    
    fn set_parameter(&mut self, index: u32, value: f32) -> Result<(), PluginError> {
        if index < self.num_parameters() {
            self.plugin.set_parameter(index as i32, value);
            Ok(())
        } else {
            Err(PluginError::InvalidParameter(index))
        }
    }
    
    // ... other trait implementations
}
```

### GUI Component System

```rust
use egui::{Context, Ui, Response, Sense};

pub struct PluginComponent {
    pub id: u32,
    pub name: String,
    pub position: (f32, f32),
    pub size: (f32, f32),
    pub bypass_state: Arc<AtomicBool>,
    pub editor_open: bool,
    
    // UI state
    dragging: bool,
    drag_offset: (f32, f32),
    
    // Event channels
    parameter_sender: Sender<ParameterUpdate>,
    editor_commands: Sender<EditorCommand>,
}

impl PluginComponent {
    pub fn ui(&mut self, ui: &mut Ui, ctx: &Context) -> Response {
        let rect = egui::Rect::from_min_size(
            egui::pos2(self.position.0, self.position.1),
            egui::vec2(self.size.0, self.size.1)
        );
        
        let response = ui.allocate_rect(rect, Sense::click_and_drag());
        
        // Draw plugin background and border
        ui.painter().rect_filled(
            rect, 
            egui::Rounding::same(5.0), 
            egui::Color32::from_rgb(60, 60, 80)
        );
        
        ui.painter().rect_stroke(
            rect,
            egui::Rounding::same(5.0),
            egui::Stroke::new(2.0, egui::Color32::from_rgb(100, 100, 120))
        );
        
        // Plugin title
        ui.painter().text(
            egui::pos2(rect.min.x + 5.0, rect.min.y + 5.0),
            egui::Align2::LEFT_TOP,
            &self.name,
            egui::FontId::proportional(14.0),
            egui::Color32::WHITE
        );
        
        // Control buttons
        let button_y = rect.max.y - 30.0;
        
        // Edit button
        if ui.put(
            egui::Rect::from_min_size(
                egui::pos2(rect.min.x + 10.0, button_y),
                egui::vec2(20.0, 20.0)
            ),
            egui::Button::new("E")
        ).clicked() {
            self.editor_commands.send(EditorCommand::Open(self.id)).ok();
        }
        
        // Mappings button
        if ui.put(
            egui::Rect::from_min_size(
                egui::pos2(rect.min.x + 35.0, button_y),
                egui::vec2(20.0, 20.0)
            ),
            egui::Button::new("M")
        ).clicked() {
            self.editor_commands.send(EditorCommand::OpenMappings(self.id)).ok();
        }
        
        // Bypass button
        let bypass = self.bypass_state.load(Ordering::Relaxed);
        let bypass_color = if bypass { 
            egui::Color32::RED 
        } else { 
            egui::Color32::GREEN 
        };
        
        if ui.put(
            egui::Rect::from_min_size(
                egui::pos2(rect.max.x - 30.0, button_y),
                egui::vec2(20.0, 20.0)
            ),
            egui::Button::new("B").fill(bypass_color)
        ).clicked() {
            self.bypass_state.store(!bypass, Ordering::Relaxed);
        }
        
        // Handle dragging
        if response.dragged() {
            if !self.dragging {
                self.dragging = true;
                if let Some(pointer_pos) = response.interact_pointer_pos() {
                    self.drag_offset = (
                        pointer_pos.x - self.position.0,
                        pointer_pos.y - self.position.1
                    );
                }
            }
            
            if let Some(pointer_pos) = response.interact_pointer_pos() {
                self.position = (
                    pointer_pos.x - self.drag_offset.0,
                    pointer_pos.y - self.drag_offset.1
                );
            }
        } else {
            self.dragging = false;
        }
        
        response
    }
}
```

## Plugin Format Support

### VST 2.4 Support
- Use `vst` crate for VST 2.4 plugin loading
- Wrapper handles parameter automation
- Custom GUI integration with host's UI framework

### Audio Units (macOS)
```rust
#[cfg(target_os = "macos")]
pub struct AudioUnitWrapper {
    unit: AudioUnit,
    component_description: AudioComponentDescription,
}

#[cfg(target_os = "macos")]
impl AudioUnitWrapper {
    pub fn new(component_type: OSType, component_subtype: OSType) -> Result<Self, PluginError> {
        let desc = AudioComponentDescription {
            componentType: component_type,
            componentSubType: component_subtype,
            componentManufacturer: 0,
            componentFlags: 0,
            componentFlagsMask: 0,
        };
        
        let unit = AudioUnit::new(desc)
            .map_err(|e| PluginError::LoadFailed(e.to_string()))?;
            
        Ok(Self {
            unit,
            component_description: desc,
        })
    }
}
```

### LADSPA Support
```rust
pub struct LadspaWrapper {
    descriptor: *const ladspa::Descriptor,
    instance: *mut c_void,
    ports: Vec<PortConnection>,
    control_ports: Vec<f32>,
}

impl LadspaWrapper {
    pub fn load_from_library(lib_path: &Path, index: u32) -> Result<Self, PluginError> {
        unsafe {
            let library = libloading::Library::new(lib_path)
                .map_err(|e| PluginError::LoadFailed(e.to_string()))?;
                
            let descriptor_fn: libloading::Symbol<unsafe extern "C" fn(u32) -> *const ladspa::Descriptor> = 
                library.get(b"ladspa_descriptor")
                .map_err(|e| PluginError::LoadFailed(e.to_string()))?;
                
            let descriptor = descriptor_fn(index);
            if descriptor.is_null() {
                return Err(PluginError::LoadFailed("Invalid LADSPA descriptor".into()));
            }
            
            let instantiate = (*descriptor).instantiate
                .ok_or(PluginError::LoadFailed("No instantiate function".into()))?;
                
            let instance = instantiate(descriptor, 44100);
            if instance.is_null() {
                return Err(PluginError::LoadFailed("Failed to instantiate plugin".into()));
            }
            
            Ok(Self {
                descriptor,
                instance,
                ports: Vec::new(),
                control_ports: Vec::new(),
            })
        }
    }
}
```

## Memory Safety and Performance

### Real-time Safety Guarantees
```rust
// All audio thread operations must be allocation-free
fn process_realtime_safe() {
    // ✅ Pre-allocated buffers
    // ✅ Lock-free data structures  
    // ✅ Atomic operations for state
    // ❌ No heap allocation
    // ❌ No blocking operations
    // ❌ No system calls
}
```

### Error Handling Strategy
```rust
#[derive(Debug, thiserror::Error)]
pub enum PluginError {
    #[error("Failed to load plugin: {0}")]
    LoadFailed(String),
    
    #[error("Plugin processing error: {0}")]
    ProcessingError(String),
    
    #[error("Invalid parameter index: {0}")]
    InvalidParameter(u32),
    
    #[error("State serialization error: {0}")]
    StateError(String),
    
    #[error("Thread communication error")]
    ChannelError,
}

// Non-panicking error handling in audio thread
impl PluginWrapper {
    fn process_with_error_handling(&mut self, inputs: &[&[f32]], outputs: &mut [&mut [f32]]) {
        match self.process_block(inputs, outputs, &mut MidiBuffer::new()) {
            Ok(()) => {}, // Normal processing
            Err(e) => {
                // Log error and pass audio through unchanged
                eprintln!("Plugin error: {}", e);
                self.pass_through(inputs, outputs);
            }
        }
    }
}
```

## Migration Timeline

### Phase 2.1: Core Plugin Infrastructure (Weeks 1-2)
- Implement `AudioPlugin` trait system
- Create `PluginWrapper` with bypass functionality
- Basic VST 2.4 support using `vst` crate
- Plugin loading and instantiation

### Phase 2.2: GUI Integration (Weeks 3-4)  
- `PluginComponent` implementation in chosen GUI framework
- Plugin editor window management
- Drag-and-drop functionality
- Basic plugin parameter controls

### Phase 2.3: Extended Format Support (Weeks 5-6)
- LADSPA plugin support
- Audio Units support (macOS)
- Plugin scanning and caching
- Preset management system

### Phase 2.4: Advanced Features (Weeks 7-8)
- Real-time parameter automation
- MIDI channel filtering
- Plugin crash recovery
- State serialization/deserialization

## Risk Mitigation

### Plugin Compatibility
- Extensive testing with popular plugin libraries
- Fallback mechanisms for unsupported features
- Plugin sandboxing to prevent crashes

### Performance Concerns
- Benchmark against original JUCE implementation
- Profile memory allocation patterns
- Optimize critical audio processing paths

### Cross-platform Issues
- Platform-specific plugin format support
- Audio driver integration differences
- GUI framework compatibility

This migration strategy ensures that Pedalboard3's plugin system maintains full compatibility with existing plugins while leveraging Rust's safety features for improved stability and performance.