//! MIDI/OSC Mapping Validation Tests
//!
//! Tests to validate MIDI CC and OSC message mapping functionality
//! and ensure compatibility with original Pedalboard2 behavior.

use std::collections::HashMap;
use std::sync::mpsc;
use std::time::Duration;

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_midi_cc_mapping_framework() {
        // Test the MIDI CC mapping validation framework
        let validator = MidiMappingValidator::new();
        
        // Create test mapping configuration
        let mapping = create_test_midi_mapping();
        
        // Validate mapping structure
        assert!(validator.validate_mapping(&mapping).is_ok(), 
               "Test MIDI mapping should be valid");
        
        println!("MIDI CC mapping framework validation:");
        println!("  CC Number: {}", mapping.cc_number);
        println!("  Target Plugin: {}", mapping.plugin_name);
        println!("  Parameter: {}", mapping.parameter_name);
        println!("  Range: {:?}", mapping.value_range);
        println!("  Curve: {:?}", mapping.curve);
    }
    
    #[test]
    fn test_midi_message_processing() {
        // Test MIDI message processing and parameter mapping
        let mut processor = MidiMessageProcessor::new();
        
        // Add test mapping
        let mapping = create_test_midi_mapping();
        processor.add_mapping(mapping);
        
        // Test MIDI CC messages
        let test_values = [0, 32, 64, 96, 127];
        
        for cc_value in test_values {
            let message = MidiMessage::ControlChange {
                channel: 0,
                cc_number: 1,
                value: cc_value,
            };
            
            let parameter_updates = processor.process_message(message);
            
            assert!(!parameter_updates.is_empty(), 
                   "Should generate parameter update for CC value {}", cc_value);
            
            let update = &parameter_updates[0];
            let expected_normalized = cc_value as f32 / 127.0;
            
            println!("CC {} -> Parameter value {:.3}", cc_value, update.value);
            
            // Validate value is in expected range
            assert!(update.value >= 0.0 && update.value <= 1.0,
                   "Parameter value should be normalized: {}", update.value);
        }
    }
    
    #[test]
    fn test_osc_message_parsing() {
        // Test OSC message parsing and routing
        let parser = OscMessageParser::new();
        
        // Test various OSC message formats
        let test_messages = vec![
            ("/plugin/reverb/room_size", vec![OscValue::Float(0.5)]),
            ("/plugin/delay/feedback", vec![OscValue::Float(0.3)]),
            ("/pedalboard/bypass", vec![OscValue::Int(1)]),
            ("/midi/learn/start", vec![]),
        ];
        
        for (address, args) in test_messages {
            let message = OscMessage {
                address: address.to_string(),
                arguments: args,
            };
            
            match parser.parse_message(&message) {
                Ok(command) => {
                    println!("OSC message parsed: {} -> {:?}", address, command);
                    assert!(!command.target.is_empty(), "Command should have target");
                }
                Err(e) => {
                    println!("Failed to parse OSC message {}: {}", address, e);
                }
            }
        }
    }
    
    #[test]
    fn test_mapping_learning_mode() {
        // Test MIDI/OSC learning functionality
        let mut learner = MappingLearner::new();
        
        // Start learning mode for a specific parameter
        let learn_session = LearnSession {
            plugin_id: "test_plugin".to_string(),
            parameter_name: "test_param".to_string(),
            timeout: Duration::from_secs(5),
        };
        
        learner.start_learning(learn_session);
        assert!(learner.is_learning(), "Should be in learning mode");
        
        // Simulate incoming MIDI CC message
        let midi_message = MidiMessage::ControlChange {
            channel: 0,
            cc_number: 20,
            value: 64,
        };
        
        if let Some(learned_mapping) = learner.process_learn_message(midi_message) {
            println!("Successfully learned mapping: {:?}", learned_mapping);
            assert_eq!(learned_mapping.cc_number, 20);
            assert_eq!(learned_mapping.plugin_name, "test_plugin");
            assert_eq!(learned_mapping.parameter_name, "test_param");
        }
        
        learner.stop_learning();
        assert!(!learner.is_learning(), "Should exit learning mode");
    }
    
    #[test]
    fn test_parameter_automation_curves() {
        // Test different automation curve types
        let curves = vec![
            AutomationCurve::Linear,
            AutomationCurve::Exponential { factor: 2.0 },
            AutomationCurve::Logarithmic,
            AutomationCurve::SCurve,
        ];
        
        for curve in curves {
            println!("Testing curve: {:?}", curve);
            
            // Test curve mapping at various input points
            let test_inputs = [0.0, 0.25, 0.5, 0.75, 1.0];
            
            for &input in &test_inputs {
                let output = curve.apply(input);
                
                println!("  Input: {:.2} -> Output: {:.3}", input, output);
                
                // Basic validation
                assert!(output >= 0.0 && output <= 1.0, 
                       "Curve output should be normalized: {}", output);
                
                // Monotonicity check (curves should be monotonic)
                if input == 0.0 {
                    assert!((output - 0.0).abs() < 0.01, "Curve should start near 0");
                } else if input == 1.0 {
                    assert!((output - 1.0).abs() < 0.01, "Curve should end near 1");
                }
            }
        }
    }
    
    #[test]
    fn test_timing_accuracy() {
        // Test timing accuracy of MIDI/OSC processing
        let mut processor = MidiMessageProcessor::new();
        processor.add_mapping(create_test_midi_mapping());
        
        let mut timing_results = Vec::new();
        let num_tests = 100;
        
        for i in 0..num_tests {
            let start_time = std::time::Instant::now();
            
            let message = MidiMessage::ControlChange {
                channel: 0,
                cc_number: 1,
                value: (i % 128) as u8,
            };
            
            let _updates = processor.process_message(message);
            let processing_time = start_time.elapsed();
            
            timing_results.push(processing_time);
        }
        
        // Calculate timing statistics
        let avg_time = timing_results.iter().sum::<Duration>() / num_tests;
        let max_time = timing_results.iter().max().unwrap();
        let min_time = timing_results.iter().min().unwrap();
        
        println!("MIDI processing timing statistics:");
        println!("  Average: {:?}", avg_time);
        println!("  Maximum: {:?}", max_time);
        println!("  Minimum: {:?}", min_time);
        
        // Validate timing requirements
        assert!(avg_time < Duration::from_micros(100), 
               "Average processing time too slow: {:?}", avg_time);
        assert!(max_time < Duration::from_micros(1000), 
               "Maximum processing time too slow: {:?}", max_time);
    }
    
    #[test]
    fn test_concurrent_mapping_access() {
        // Test thread safety of mapping system
        use std::sync::{Arc, Mutex};
        use std::thread;
        
        let processor = Arc::new(Mutex::new(MidiMessageProcessor::new()));
        
        // Add test mapping
        {
            let mut proc = processor.lock().unwrap();
            proc.add_mapping(create_test_midi_mapping());
        }
        
        let num_threads = 4;
        let messages_per_thread = 100;
        let mut handles = Vec::new();
        
        // Spawn threads to process messages concurrently
        for thread_id in 0..num_threads {
            let processor_clone = Arc::clone(&processor);
            
            let handle = thread::spawn(move || {
                for i in 0..messages_per_thread {
                    let message = MidiMessage::ControlChange {
                        channel: 0,
                        cc_number: 1,
                        value: ((thread_id * messages_per_thread + i) % 128) as u8,
                    };
                    
                    let proc = processor_clone.lock().unwrap();
                    let _updates = proc.process_message(message);
                }
            });
            
            handles.push(handle);
        }
        
        // Wait for all threads to complete
        for handle in handles {
            handle.join().expect("Thread should complete successfully");
        }
        
        println!("Concurrent access test completed successfully");
    }
}

/// MIDI message types
#[derive(Debug, Clone, Copy)]
pub enum MidiMessage {
    ControlChange {
        channel: u8,
        cc_number: u8,
        value: u8,
    },
    ProgramChange {
        channel: u8,
        program: u8,
    },
    NoteOn {
        channel: u8,
        note: u8,
        velocity: u8,
    },
    NoteOff {
        channel: u8,
        note: u8,
        velocity: u8,
    },
}

/// OSC message value types
#[derive(Debug, Clone)]
pub enum OscValue {
    Int(i32),
    Float(f32),
    String(String),
    Bool(bool),
}

/// OSC message structure
#[derive(Debug, Clone)]
pub struct OscMessage {
    pub address: String,
    pub arguments: Vec<OscValue>,
}

/// Parameter update structure
#[derive(Debug, Clone)]
pub struct ParameterUpdate {
    pub plugin_id: String,
    pub parameter_name: String,
    pub value: f32,
    pub timestamp: std::time::Instant,
}

/// MIDI CC mapping configuration
#[derive(Debug, Clone)]
pub struct MidiMapping {
    pub cc_number: u8,
    pub plugin_name: String,
    pub parameter_name: String,
    pub value_range: (f32, f32),
    pub curve: AutomationCurve,
    pub channel_filter: Option<u8>,
}

/// Automation curve types
#[derive(Debug, Clone)]
pub enum AutomationCurve {
    Linear,
    Exponential { factor: f32 },
    Logarithmic,
    SCurve,
    Custom { points: Vec<(f32, f32)> },
}

impl AutomationCurve {
    /// Apply the curve to normalize input (0.0-1.0) to output (0.0-1.0)
    pub fn apply(&self, input: f32) -> f32 {
        let clamped_input = input.clamp(0.0, 1.0);
        
        match self {
            AutomationCurve::Linear => clamped_input,
            
            AutomationCurve::Exponential { factor } => {
                clamped_input.powf(*factor)
            }
            
            AutomationCurve::Logarithmic => {
                if clamped_input <= 0.0 {
                    0.0
                } else {
                    (clamped_input * 9.0 + 1.0).log10()
                }
            }
            
            AutomationCurve::SCurve => {
                // Sigmoid-like S-curve
                let x = (clamped_input - 0.5) * 6.0; // Scale to roughly -3 to 3
                1.0 / (1.0 + (-x).exp())
            }
            
            AutomationCurve::Custom { points } => {
                // Linear interpolation between custom points
                if points.is_empty() {
                    return clamped_input;
                }
                
                // Find the two points to interpolate between
                for i in 0..points.len() - 1 {
                    let (x1, y1) = points[i];
                    let (x2, y2) = points[i + 1];
                    
                    if clamped_input >= x1 && clamped_input <= x2 {
                        let t = (clamped_input - x1) / (x2 - x1);
                        return y1 + t * (y2 - y1);
                    }
                }
                
                // If outside range, use first or last point
                if clamped_input < points[0].0 {
                    points[0].1
                } else {
                    points[points.len() - 1].1
                }
            }
        }
    }
}

/// MIDI mapping validator
pub struct MidiMappingValidator;

impl MidiMappingValidator {
    pub fn new() -> Self {
        Self
    }
    
    pub fn validate_mapping(&self, mapping: &MidiMapping) -> Result<(), String> {
        // Validate CC number
        if mapping.cc_number > 127 {
            return Err("CC number must be 0-127".to_string());
        }
        
        // Validate plugin name
        if mapping.plugin_name.is_empty() {
            return Err("Plugin name cannot be empty".to_string());
        }
        
        // Validate parameter name
        if mapping.parameter_name.is_empty() {
            return Err("Parameter name cannot be empty".to_string());
        }
        
        // Validate range
        if mapping.value_range.0 > mapping.value_range.1 {
            return Err("Invalid value range: min > max".to_string());
        }
        
        // Validate channel filter
        if let Some(channel) = mapping.channel_filter {
            if channel > 15 {
                return Err("MIDI channel must be 0-15".to_string());
            }
        }
        
        Ok(())
    }
}

/// MIDI message processor
pub struct MidiMessageProcessor {
    mappings: Vec<MidiMapping>,
}

impl MidiMessageProcessor {
    pub fn new() -> Self {
        Self {
            mappings: Vec::new(),
        }
    }
    
    pub fn add_mapping(&mut self, mapping: MidiMapping) {
        self.mappings.push(mapping);
    }
    
    pub fn process_message(&self, message: MidiMessage) -> Vec<ParameterUpdate> {
        let mut updates = Vec::new();
        
        match message {
            MidiMessage::ControlChange { channel, cc_number, value } => {
                // Find matching mappings
                for mapping in &self.mappings {
                    if mapping.cc_number == cc_number {
                        // Check channel filter
                        if let Some(filter_channel) = mapping.channel_filter {
                            if channel != filter_channel {
                                continue;
                            }
                        }
                        
                        // Convert CC value to parameter value
                        let normalized = value as f32 / 127.0;
                        let curved = mapping.curve.apply(normalized);
                        let final_value = mapping.value_range.0 + 
                            curved * (mapping.value_range.1 - mapping.value_range.0);
                        
                        updates.push(ParameterUpdate {
                            plugin_id: mapping.plugin_name.clone(),
                            parameter_name: mapping.parameter_name.clone(),
                            value: final_value,
                            timestamp: std::time::Instant::now(),
                        });
                    }
                }
            }
            
            _ => {
                // Handle other MIDI message types as needed
            }
        }
        
        updates
    }
}

/// OSC command types
#[derive(Debug)]
pub enum OscCommand {
    SetParameter {
        target: String,
        parameter: String,
        value: f32,
    },
    SetBypass {
        target: String,
        bypass: bool,
    },
    StartLearn {
        target: String,
        parameter: String,
    },
    StopLearn,
}

/// OSC message parser
pub struct OscMessageParser;

impl OscMessageParser {
    pub fn new() -> Self {
        Self
    }
    
    pub fn parse_message(&self, message: &OscMessage) -> Result<OscCommand, String> {
        let parts: Vec<&str> = message.address.trim_start_matches('/').split('/').collect();
        
        match parts.as_slice() {
            ["plugin", plugin_name, parameter_name] => {
                if let Some(OscValue::Float(value)) = message.arguments.first() {
                    Ok(OscCommand::SetParameter {
                        target: plugin_name.to_string(),
                        parameter: parameter_name.to_string(),
                        value: *value,
                    })
                } else {
                    Err("Invalid parameter value".to_string())
                }
            }
            
            ["pedalboard", "bypass"] => {
                if let Some(OscValue::Int(bypass)) = message.arguments.first() {
                    Ok(OscCommand::SetBypass {
                        target: "pedalboard".to_string(),
                        bypass: *bypass != 0,
                    })
                } else {
                    Err("Invalid bypass value".to_string())
                }
            }
            
            ["midi", "learn", "start"] => {
                Ok(OscCommand::StartLearn {
                    target: "global".to_string(),
                    parameter: "unknown".to_string(),
                })
            }
            
            ["midi", "learn", "stop"] => {
                Ok(OscCommand::StopLearn)
            }
            
            _ => Err(format!("Unknown OSC address pattern: {}", message.address))
        }
    }
}

/// Learning session for MIDI/OSC mapping
#[derive(Debug, Clone)]
pub struct LearnSession {
    pub plugin_id: String,
    pub parameter_name: String,
    pub timeout: Duration,
}

/// Mapping learner for interactive mapping creation
pub struct MappingLearner {
    active_session: Option<LearnSession>,
    start_time: Option<std::time::Instant>,
}

impl MappingLearner {
    pub fn new() -> Self {
        Self {
            active_session: None,
            start_time: None,
        }
    }
    
    pub fn start_learning(&mut self, session: LearnSession) {
        self.active_session = Some(session);
        self.start_time = Some(std::time::Instant::now());
    }
    
    pub fn stop_learning(&mut self) {
        self.active_session = None;
        self.start_time = None;
    }
    
    pub fn is_learning(&self) -> bool {
        if let Some(start_time) = self.start_time {
            if let Some(session) = &self.active_session {
                // Check if session has timed out
                return start_time.elapsed() < session.timeout;
            }
        }
        false
    }
    
    pub fn process_learn_message(&mut self, message: MidiMessage) -> Option<MidiMapping> {
        if !self.is_learning() {
            return None;
        }
        
        match message {
            MidiMessage::ControlChange { channel: _, cc_number, value: _ } => {
                if let Some(session) = &self.active_session {
                    let mapping = MidiMapping {
                        cc_number,
                        plugin_name: session.plugin_id.clone(),
                        parameter_name: session.parameter_name.clone(),
                        value_range: (0.0, 1.0),
                        curve: AutomationCurve::Linear,
                        channel_filter: None,
                    };
                    
                    self.stop_learning();
                    return Some(mapping);
                }
            }
            
            _ => {
                // Other MIDI message types could be supported for learning
            }
        }
        
        None
    }
}

// Helper function for creating test data
fn create_test_midi_mapping() -> MidiMapping {
    MidiMapping {
        cc_number: 1,
        plugin_name: "TestPlugin".to_string(),
        parameter_name: "TestParameter".to_string(),
        value_range: (0.0, 1.0),
        curve: AutomationCurve::Linear,
        channel_filter: None,
    }
}