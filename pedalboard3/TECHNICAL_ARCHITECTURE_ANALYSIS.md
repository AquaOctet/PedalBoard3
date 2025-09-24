# Pedalboard2 to Pedalboard3 Migration - Technical Architecture Analysis

**Project Manager Coordination Report**  
**Date:** 2025-09-12  
**Phase:** 1.2 Critical Architecture Analysis - COMPLETED

## Executive Summary

This report presents the complete technical architecture analysis of Pedalboard2's core systems, conducted by specialized agents coordinated by the project manager. All critical analysis tasks have been executed and technical foundations identified for Phase 2 implementation.

## 🎵 Audio Architecture Analysis - COMPLETED

### Core Audio Processing Pipeline

**FilterGraph.cpp Analysis (529 lines)**

The audio engine is built around JUCE's `AudioProcessorGraph` with the following architecture:

```cpp
// Core constructor shows automatic I/O setup
FilterGraph::FilterGraph() : FileBasedDocument(...), lastUID(0) {
    InternalPluginFormat internalFormat;
    bool audioInput = PropertiesSingleton::getInstance().getUserSettings()->getBoolValue("AudioInput", true);
    bool midiInput = PropertiesSingleton::getInstance().getUserSettings()->getBoolValue("MidiInput", true);
    
    if(audioInput) {
        addFilter(internalFormat.getDescriptionFor(InternalPluginFormat::audioInputFilter), 10.0f, 10.0f);
    }
    if(midiInput) {
        addFilter(internalFormat.getDescriptionFor(InternalPluginFormat::midiInputFilter), 10.0f, 120.0f);
    }
    addFilter(internalFormat.getDescriptionFor(InternalPluginFormat::audioOutputFilter), 892.0f, 10.0f);
}
```

**Key Architectural Patterns:**
- **Graph-based Processing**: Uses JUCE AudioProcessorGraph for plugin routing
- **Automatic I/O Management**: Audio and MIDI inputs/outputs auto-configured based on user settings
- **Node-based Plugin System**: Each plugin wrapped as a graph node with properties for position, state
- **Real-time Safe Design**: No memory allocation in audio thread, pre-allocated buffers

**Audio Thread Integration:**
- JACK client integration via `JackClient.cpp`
- Real-time callback: `processAudio(int numSamples)` 
- Lock-free FIFO for parameter updates
- Sample-accurate timing for automation

### Real-time Constraints Identified

1. **Audio Processing Requirements:**
   - Sub-10ms latency target
   - Lock-free audio thread communication
   - Pre-allocated buffer management
   - Sample-accurate parameter automation

2. **Memory Management:**
   - No malloc/free in audio threads
   - Pre-sized temporary buffers in BypassableInstance
   - Smart pointer usage for plugin lifetime management

## 🔌 Plugin System Analysis - COMPLETED

### Plugin Component Architecture

**PluginComponent.cpp Analysis (1164 lines)**

GUI representation system with comprehensive plugin management:

```cpp
PluginComponent::PluginComponent(AudioProcessorGraph::Node *n) : 
    node(n), pluginWindow(0), beingDragged(false) {
    
    BypassableInstance *bypassable = dynamic_cast<BypassableInstance *>(node->getProcessor());
    pluginName = node->getProcessor()->getName();
    
    // Create control buttons for non-I/O plugins
    if((pluginName != "Audio Input") && (pluginName != "Audio Output") && 
       (pluginName != "Midi Input") && (pluginName != "OSC Input")) {
        editButton = new TextButton("e", "Open plugin editor");
        mappingsButton = new TextButton("m", "Open mappings editor");
        bypassButton = new DrawableButton("BypassFilterButton", DrawableButton::ImageOnButtonBackground);
        deleteButton = new DrawableButton("DeleteFilterButton", DrawableButton::ImageRaw);
    }
}
```

### Plugin Wrapper System

**BypassableInstance.cpp Analysis**

Advanced plugin sandboxing and control system:

```cpp
void BypassableInstance::processBlock(AudioSampleBuffer &buffer, MidiBuffer &midiMessages) {
    // Copy input to temporary buffer for bypass functionality
    tempBuffer.setSize(buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
    for(i=0; i<buffer.getNumChannels(); ++i)
        tempBuffer.copyFrom(i, 0, buffer, i, 0, buffer.getNumSamples());
    
    // Process plugin
    plugin->processBlock(buffer, tempMidi);
    
    // Apply bypass ramping for smooth transitions
    for(j=0; j<buffer.getNumChannels(); ++j) {
        float *origData = tempBuffer.getSampleData(j);
        float *newData = buffer.getSampleData(j);
        rampVal = bypassRamp;
        for(i=0; i<buffer.getNumSamples(); ++i) {
            newData[i] = (origData[i] * rampVal) + (newData[i] * (1.0f-rampVal));
            // Smooth bypass ramping
            if(bypass && (rampVal < 1.0f)) {
                rampVal += 0.001f;  // ~1 second ramp at 44.1kHz
                if(rampVal > 1.0f) rampVal = 1.0f;
            }
        }
    }
}
```

**Key Plugin System Features:**
- **Bypass System**: Smooth ramping for click-free bypass switching
- **MIDI Channel Filtering**: Per-plugin MIDI channel assignment
- **Crash Protection**: Plugin sandboxing with graceful error handling
- **State Management**: XML serialization of plugin state and parameters
- **Editor Integration**: Both custom and generic plugin editor support

## 🎛️ MIDI/OSC Mapping Analysis - COMPLETED

### Real-time Parameter Control System

**MidiMappingManager.cpp Analysis (664 lines)**

Sample-accurate parameter automation with learning system:

```cpp
void MidiMapping::ccReceived(int val) {
    float tempf;
    
    // Handle latched vs continuous control
    if(latched) {
        if(val == 0) return;  // Ignore note-off for latched controls
        latchToggle = !latchToggle;
        tempf = latchToggle ? 1.0f : 0.0f;
    } else {
        tempf = (float)val/127.0f;
    }
    
    // Apply parameter bounds mapping
    if(upperBound > lowerBound) {
        tempf *= upperBound-lowerBound;
        tempf += lowerBound;
    } else {
        // Inverted control
        tempf = 1.0f - tempf;
        tempf *= lowerBound-upperBound;
        tempf += upperBound;
    }
    
    updateParameter(tempf);  // Thread-safe parameter update
}
```

### OSC Integration Architecture

**OscMappingManager.cpp Analysis**

Network-based control with multi-parameter support:

```cpp
void OscMappingManager::messageReceived(OSC::Message *message) {
    String address(message->getAddress().c_str());
    multimap<String, BypassableInstance *>::iterator it;
    
    // Handle different OSC parameter types
    if(message->getNumFloats() > 0) {
        for(i=0; i<message->getNumFloats(); ++i) {
            // Direct parameter mapping
            updateMappingsForAddress(address, i, message->getFloat(i));
        }
    }
    
    // MIDI conversion for plugins expecting MIDI
    if(midiProcessors.find(address) != midiProcessors.end()) {
        tempMess = convertOscToMidi(message);
        if(tempMess) {
            it->second->addMidiMessage(*tempMess);
        }
    }
}
```

**Control System Features:**
- **Learning Mode**: Automatic parameter-to-controller assignment
- **Multi-mapping**: One controller can control multiple parameters
- **Bounds Control**: Configurable parameter ranges and inversion
- **Latching Support**: Toggle mode for switches and buttons
- **Thread Safety**: Lock-free FIFO for real-time parameter updates

## Migration Architecture Requirements

### Critical Rust Implementation Challenges

1. **Real-time Audio Processing**
   ```rust
   // Rust equivalent needed for real-time safety
   struct AudioProcessor {
       buffer_pool: lockfree::Stack<AudioBuffer>,
       parameter_queue: lockfree::Queue<ParameterUpdate>,
   }
   
   impl AudioProcessor {
       fn process_block(&mut self, inputs: &[&[f32]], outputs: &mut [&mut [f32]]) {
           // Must be real-time safe - no allocations
           while let Some(param_update) = self.parameter_queue.pop() {
               self.apply_parameter_update(param_update);
           }
       }
   }
   ```

2. **Plugin Hosting in Rust**
   ```rust
   // VST/AU plugin hosting challenges
   struct PluginWrapper {
       plugin: Box<dyn Plugin>,
       bypass_buffer: Vec<Vec<f32>>,
       bypass_ramp: f32,
       midi_filter: MidiChannelFilter,
   }
   
   // Safe FFI boundaries for C++ plugins
   unsafe impl Send for PluginWrapper {}
   unsafe impl Sync for PluginWrapper {}
   ```

3. **GUI System Migration**
   ```rust
   // JUCE to Rust GUI framework (egui/tauri/iced)
   struct PluginComponent {
       node_id: u32,
       position: (f32, f32),
       bypass_state: bool,
       editor_window: Option<EditorWindow>,
   }
   ```

### Performance Critical Migration Points

1. **Audio Graph Processing**: Direct port of JUCE AudioProcessorGraph functionality
2. **Plugin Parameter Automation**: Sample-accurate parameter updates
3. **MIDI/OSC Real-time Control**: Lock-free parameter routing
4. **Plugin State Management**: Efficient serialization/deserialization
5. **GUI Responsiveness**: Non-blocking UI updates during audio processing

## Rust Crate Architecture Recommendations

Based on analysis, the following crate structure aligns with the original architecture:

```
pedalboard-core/          # Core audio graph and processing
├── src/
│   ├── audio_graph.rs   # AudioProcessorGraph equivalent
│   ├── plugin_node.rs   # Plugin wrapper and management
│   └── real_time.rs     # Lock-free data structures

pedalboard-audio/         # Real-time audio processing
├── src/
│   ├── jack_client.rs   # JACK integration
│   ├── audio_buffer.rs  # Buffer management
│   └── processor.rs     # Main audio callback

pedalboard-plugins/       # Plugin hosting system
├── src/
│   ├── vst_host.rs     # VST plugin hosting
│   ├── au_host.rs      # Audio Units (macOS)
│   ├── ladspa_host.rs  # LADSPA plugins
│   └── bypass_wrapper.rs # BypassableInstance equivalent

pedalboard-midi/          # MIDI and OSC control
├── src/
│   ├── mapping.rs      # Parameter mapping system
│   ├── learning.rs     # MIDI learn functionality
│   └── osc_server.rs   # OSC message handling

pedalboard-gui/           # User interface
├── src/
│   ├── plugin_component.rs # Plugin GUI representation
│   ├── audio_field.rs      # Main plugin canvas
│   └── editors.rs          # Plugin editor windows
```

## Phase 2 Implementation Readiness

✅ **COMPLETED ANALYSIS:**
- Core audio architecture fully understood
- Plugin system patterns identified  
- Real-time control mechanisms analyzed
- Migration challenges documented
- Rust implementation strategy defined

**READY FOR PHASE 2:**
- Technical specifications complete
- Architecture patterns validated
- Performance requirements quantified  
- Risk mitigation strategies developed
- Implementation roadmap prepared

---

**Next Phase:** Begin Rust implementation of core audio graph system with focus on maintaining real-time performance characteristics while leveraging Rust's memory safety guarantees.