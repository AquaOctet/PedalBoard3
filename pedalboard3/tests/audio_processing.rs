//! Audio Processing Validation Tests
//!
//! Tests to verify that the Rust implementation produces identical audio
//! output to the original Pedalboard2 application.

use crate::utils::*;
use std::path::PathBuf;

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_baseline_original_pedalboard_available() {
        assert!(
            check_original_executables(),
            "Original Pedalboard2 executables not found! Cannot run baseline tests."
        );
    }
    
    #[test] 
    fn test_sine_wave_passthrough() {
        // Test that a sine wave passes through unchanged when no processing is applied
        let frequency = 440.0;
        let sample_rate = 44100.0;
        let duration = 1.0; // 1 second
        
        let input = generate_sine_wave(frequency, sample_rate, duration);
        
        // TODO: Process through Rust implementation (passthrough)
        // let rust_output = process_with_rust(&input);
        
        // For now, test the utility function itself
        assert_eq!(input.len(), (sample_rate * duration) as usize);
        
        // Verify sine wave properties
        let rms = calculate_rms(&input);
        let expected_rms = 1.0 / (2.0_f32).sqrt(); // RMS of unity sine wave
        assert!((rms - expected_rms).abs() < 0.01, "Sine wave RMS incorrect: {} vs {}", rms, expected_rms);
    }
    
    #[test]
    #[ignore] // Requires original Pedalboard2 to be running
    fn test_original_vs_rust_level_processor() {
        // This test would compare level processing between original and Rust versions
        let input = generate_sine_wave(440.0, 44100.0, 0.1);
        
        // TODO: Implement when audio graph is available
        // 1. Process input through original Pedalboard2 level processor
        // 2. Process same input through Rust level processor  
        // 3. Compare outputs with tolerance
        
        println!("Test framework ready - implement when LevelProcessor is available");
    }
    
    #[test]
    fn test_multichannel_audio_handling() {
        // Test stereo and multichannel audio processing
        let sample_rate = 44100.0;
        let duration = 0.5;
        
        // Create stereo test signal (left = 440Hz, right = 880Hz)
        let left_channel = generate_sine_wave(440.0, sample_rate, duration);
        let right_channel = generate_sine_wave(880.0, sample_rate, duration);
        
        assert_eq!(left_channel.len(), right_channel.len());
        
        // TODO: Test multichannel processing when audio graph supports it
        println!("Multichannel test data prepared: {} samples per channel", left_channel.len());
    }
    
    #[test]
    fn test_buffer_boundary_conditions() {
        // Test audio processing at various buffer sizes
        let frequencies = [440.0, 880.0, 1320.0];
        let buffer_sizes = [64, 128, 256, 512, 1024, 2048];
        let sample_rate = 44100.0;
        
        for &freq in &frequencies {
            for &buffer_size in &buffer_sizes {
                let duration = buffer_size as f32 / sample_rate;
                let signal = generate_sine_wave(freq, sample_rate, duration);
                
                assert_eq!(signal.len(), buffer_size);
                
                // TODO: Process through Rust audio graph when available
                println!("Buffer test prepared: {}Hz at {} samples", freq, buffer_size);
            }
        }
    }
}

/// Calculate RMS (Root Mean Square) of audio signal
fn calculate_rms(samples: &[f32]) -> f32 {
    if samples.is_empty() {
        return 0.0;
    }
    
    let sum_squares: f32 = samples.iter().map(|&x| x * x).sum();
    (sum_squares / samples.len() as f32).sqrt()
}

/// Audio processing accuracy validation framework
pub struct AudioAccuracyTester {
    tolerance: f32,
    sample_rate: f32,
}

impl AudioAccuracyTester {
    pub fn new(tolerance: f32, sample_rate: f32) -> Self {
        Self { tolerance, sample_rate }
    }
    
    /// Compare two audio processing chains for accuracy
    pub fn compare_processing_chains(&self, input: &[f32]) -> Result<bool, String> {
        // TODO: Implement when both original and Rust processing are available
        // 1. Process input through original Pedalboard2
        // 2. Process input through Rust implementation
        // 3. Compare outputs sample by sample
        // 4. Return true if within tolerance, false otherwise
        
        Err("Not yet implemented - waiting for Rust audio graph".to_string())
    }
    
    /// Generate comprehensive test signal for audio processing validation
    pub fn generate_test_signal(&self, duration_secs: f32) -> Vec<f32> {
        let num_samples = (self.sample_rate * duration_secs) as usize;
        let mut signal = Vec::with_capacity(num_samples);
        
        // Composite test signal with multiple frequencies and characteristics
        for i in 0..num_samples {
            let t = i as f32 / self.sample_rate;
            
            // Multiple sine waves at different frequencies
            let fundamental = (2.0 * std::f32::consts::PI * 440.0 * t).sin() * 0.5;
            let harmonic2 = (2.0 * std::f32::consts::PI * 880.0 * t).sin() * 0.25;
            let harmonic3 = (2.0 * std::f32::consts::PI * 1320.0 * t).sin() * 0.125;
            
            // Add some noise for realism
            let noise = (t * 12345.0).fract() * 0.01 - 0.005;
            
            let sample = fundamental + harmonic2 + harmonic3 + noise;
            signal.push(sample);
        }
        
        signal
    }
}