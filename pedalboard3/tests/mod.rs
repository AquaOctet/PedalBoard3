//! Integration tests for Pedalboard3
//! 
//! This module provides comprehensive testing infrastructure for validating
//! the Rust implementation against the original Pedalboard2 application.

pub mod audio_processing;
pub mod plugin_compatibility; 
pub mod midi_osc_mapping;
pub mod performance_benchmarks;
pub mod patch_migration;
pub mod compatibility_validation;

use std::path::PathBuf;

/// Test configuration and utilities
pub struct TestConfig {
    pub original_pedalboard_path: PathBuf,
    pub test_audio_dir: PathBuf,
    pub test_plugins_dir: PathBuf,
    pub benchmark_iterations: usize,
    pub audio_tolerance: f32,
}

impl Default for TestConfig {
    fn default() -> Self {
        Self {
            original_pedalboard_path: PathBuf::from("/mnt/d/main/DEV/pedalboard"),
            test_audio_dir: PathBuf::from("tests/audio_samples"),
            test_plugins_dir: PathBuf::from("tests/test_plugins"),
            benchmark_iterations: 1000,
            audio_tolerance: 0.001, // 0.1% tolerance for audio comparison
        }
    }
}

/// Common test utilities
pub mod utils {
    use std::process::Command;
    use std::path::Path;
    
    /// Generate test audio signals
    pub fn generate_sine_wave(frequency: f32, sample_rate: f32, duration_secs: f32) -> Vec<f32> {
        let num_samples = (sample_rate * duration_secs) as usize;
        let mut samples = Vec::with_capacity(num_samples);
        
        for i in 0..num_samples {
            let t = i as f32 / sample_rate;
            let sample = (2.0 * std::f32::consts::PI * frequency * t).sin();
            samples.push(sample);
        }
        
        samples
    }
    
    /// Compare two audio buffers with tolerance
    pub fn audio_buffers_equal(a: &[f32], b: &[f32], tolerance: f32) -> bool {
        if a.len() != b.len() {
            return false;
        }
        
        for (sample_a, sample_b) in a.iter().zip(b.iter()) {
            if (sample_a - sample_b).abs() > tolerance {
                return false;
            }
        }
        
        true
    }
    
    /// Check if original Pedalboard2 executables are available
    pub fn check_original_executables() -> bool {
        Path::new("/mnt/d/main/DEV/pedalboard/Pedalboard2.exe").exists() &&
        Path::new("/mnt/d/main/DEV/pedalboard/Pedalboard2-64bit.exe").exists()
    }
    
    /// Run original Pedalboard2 for baseline testing
    pub fn run_original_pedalboard(args: &[&str]) -> Result<std::process::Output, std::io::Error> {
        Command::new("/mnt/d/main/DEV/pedalboard/Pedalboard2-64bit.exe")
            .args(args)
            .output()
    }
}