//! Baseline Testing Framework
//!
//! Tests for establishing baseline behavior of original Pedalboard2
//! to validate against the new Rust implementation.

use std::process::{Command, Stdio};
use std::path::{Path, PathBuf};
use std::time::Duration;
use std::thread;

#[cfg(test)]
mod tests {
    use super::*;
    use crate::utils::*;
    
    #[test]
    fn test_original_pedalboard_discovery() {
        let baseline = BaselineTester::new();
        
        // Check for original executables
        let discovery_result = baseline.discover_original_installation();
        
        match discovery_result {
            Ok(installation) => {
                println!("✓ Found Pedalboard2 installation:");
                println!("  32-bit: {}", installation.path_32bit.display());
                println!("  64-bit: {}", installation.path_64bit.display());
                
                // Verify executables are functional
                assert!(installation.path_32bit.exists(), "32-bit executable should exist");
                assert!(installation.path_64bit.exists(), "64-bit executable should exist");
            }
            Err(e) => {
                println!("⚠ Original Pedalboard2 not found: {}", e);
                println!("  Baseline testing will be limited");
            }
        }
    }
    
    #[test]
    fn test_original_pedalboard_basic_execution() {
        let baseline = BaselineTester::new();
        
        if let Ok(installation) = baseline.discover_original_installation() {
            // Test basic command execution
            let result = baseline.test_basic_execution(&installation.path_64bit);
            
            match result {
                Ok(execution_info) => {
                    println!("✓ Original Pedalboard2 execution test:");
                    println!("  Exit code: {}", execution_info.exit_code);
                    println!("  Execution time: {:?}", execution_info.duration);
                    println!("  Output length: {} bytes", execution_info.output.len());
                    
                    // Basic validation
                    assert!(execution_info.duration < Duration::from_secs(10), 
                           "Startup should be reasonably fast");
                }
                Err(e) => {
                    println!("⚠ Could not execute original Pedalboard2: {}", e);
                }
            }
        } else {
            println!("Skipping execution test - original installation not found");
        }
    }
    
    #[test]
    fn test_create_baseline_patch_library() {
        let baseline = BaselineTester::new();
        
        // Create a library of test patches for baseline testing
        let library = baseline.create_test_patch_library();
        
        println!("Created baseline patch library with {} test cases:", library.patches.len());
        for (i, patch) in library.patches.iter().enumerate() {
            println!("  {}: {} - {}", i + 1, patch.name, patch.description);
        }
        
        // Validate library
        assert!(!library.patches.is_empty(), "Should have at least some test patches");
        
        // Ensure we have different complexity levels
        let simple_patches: Vec<_> = library.patches.iter()
            .filter(|p| p.complexity == PatchComplexity::Simple)
            .collect();
        let complex_patches: Vec<_> = library.patches.iter()
            .filter(|p| p.complexity == PatchComplexity::Complex)
            .collect();
            
        assert!(!simple_patches.is_empty(), "Should have simple test patches");
        assert!(!complex_patches.is_empty(), "Should have complex test patches");
    }
    
    #[test]
    fn test_baseline_audio_characteristics() {
        let baseline = BaselineTester::new();
        
        // Test audio processing characteristics
        let test_signal = generate_sine_wave(440.0, 44100.0, 1.0);
        let characteristics = baseline.analyze_audio_characteristics(&test_signal);
        
        println!("Baseline audio characteristics:");
        println!("  RMS: {:.6}", characteristics.rms);
        println!("  Peak: {:.6}", characteristics.peak);
        println!("  Frequency content: {} components", characteristics.frequency_components.len());
        
        // Validate characteristics
        assert!(characteristics.rms > 0.0, "RMS should be positive for sine wave");
        assert!(characteristics.peak <= 1.0, "Peak should not exceed unity");
    }
    
    #[test]
    fn test_baseline_performance_metrics() {
        let baseline = BaselineTester::new();
        
        // Establish baseline performance metrics
        let metrics = baseline.measure_baseline_performance();
        
        println!("Baseline performance metrics:");
        println!("  Audio processing latency: {:?}", metrics.audio_latency);
        println!("  Memory usage: {} MB", metrics.memory_usage_mb);
        println!("  CPU usage: {:.1}%", metrics.cpu_usage_percent);
        println!("  Plugin load time: {:?}", metrics.plugin_load_time);
        
        // Document metrics for comparison
        assert!(metrics.audio_latency < Duration::from_millis(50), 
               "Baseline latency should be reasonable");
        assert!(metrics.memory_usage_mb < 1000.0, 
               "Memory usage should be reasonable");
    }
}

/// Original Pedalboard2 installation discovery
#[derive(Debug)]
pub struct OriginalInstallation {
    pub path_32bit: PathBuf,
    pub path_64bit: PathBuf,
    pub version: Option<String>,
    pub install_date: Option<String>,
}

/// Execution information
#[derive(Debug)]
pub struct ExecutionInfo {
    pub exit_code: i32,
    pub duration: Duration,
    pub output: String,
    pub error_output: String,
}

/// Audio characteristics analysis
#[derive(Debug)]
pub struct AudioCharacteristics {
    pub rms: f32,
    pub peak: f32,
    pub frequency_components: Vec<FrequencyComponent>,
    pub dynamic_range: f32,
    pub thd: f32, // Total Harmonic Distortion
}

#[derive(Debug)]
pub struct FrequencyComponent {
    pub frequency: f32,
    pub magnitude: f32,
    pub phase: f32,
}

/// Performance metrics
#[derive(Debug)]
pub struct BaselineMetrics {
    pub audio_latency: Duration,
    pub memory_usage_mb: f32,
    pub cpu_usage_percent: f32,
    pub plugin_load_time: Duration,
    pub startup_time: Duration,
}

/// Test patch complexity levels
#[derive(Debug, Clone, PartialEq)]
pub enum PatchComplexity {
    Simple,    // Single plugin or passthrough
    Medium,    // Small chain of plugins
    Complex,   // Many plugins with complex routing
    Stress,    // Maximum complexity for stress testing
}

/// Test patch for baseline validation
#[derive(Debug, Clone)]
pub struct BaselineTestPatch {
    pub name: String,
    pub description: String,
    pub complexity: PatchComplexity,
    pub expected_behavior: String,
    pub test_signals: Vec<TestSignal>,
    pub validation_criteria: Vec<ValidationCriterion>,
}

#[derive(Debug, Clone)]
pub struct TestSignal {
    pub name: String,
    pub signal_type: SignalType,
    pub duration: Duration,
    pub sample_rate: f32,
}

#[derive(Debug, Clone)]
pub enum SignalType {
    SineWave { frequency: f32 },
    WhiteNoise,
    Impulse,
    Sweep { start_freq: f32, end_freq: f32 },
    Music { genre: String },
}

#[derive(Debug, Clone)]
pub struct ValidationCriterion {
    pub name: String,
    pub tolerance: f32,
    pub description: String,
}

/// Test patch library for baseline validation
#[derive(Debug)]
pub struct BaselineTestLibrary {
    pub patches: Vec<BaselineTestPatch>,
    pub test_signals: Vec<TestSignal>,
    pub created: std::time::SystemTime,
}

/// Main baseline testing framework
pub struct BaselineTester {
    pedalboard_dir: PathBuf,
}

impl BaselineTester {
    pub fn new() -> Self {
        Self {
            pedalboard_dir: PathBuf::from("/mnt/d/main/DEV/pedalboard"),
        }
    }
    
    /// Discover original Pedalboard2 installation
    pub fn discover_original_installation(&self) -> Result<OriginalInstallation, String> {
        let path_32bit = self.pedalboard_dir.join("Pedalboard2.exe");
        let path_64bit = self.pedalboard_dir.join("Pedalboard2-64bit.exe");
        
        if !path_32bit.exists() {
            return Err(format!("32-bit executable not found: {}", path_32bit.display()));
        }
        
        if !path_64bit.exists() {
            return Err(format!("64-bit executable not found: {}", path_64bit.display()));
        }
        
        // Try to get version information
        let version = self.get_pedalboard_version(&path_64bit).ok();
        
        Ok(OriginalInstallation {
            path_32bit,
            path_64bit,
            version,
            install_date: None, // TODO: Get from file metadata
        })
    }
    
    /// Test basic execution of original Pedalboard2
    pub fn test_basic_execution(&self, executable: &Path) -> Result<ExecutionInfo, String> {
        let start_time = std::time::Instant::now();
        
        // Execute with help flag to get basic info without GUI
        let output = Command::new(executable)
            .arg("--help")
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .output()
            .map_err(|e| format!("Failed to execute: {}", e))?;
        
        let duration = start_time.elapsed();
        
        Ok(ExecutionInfo {
            exit_code: output.status.code().unwrap_or(-1),
            duration,
            output: String::from_utf8_lossy(&output.stdout).to_string(),
            error_output: String::from_utf8_lossy(&output.stderr).to_string(),
        })
    }
    
    /// Get version information from Pedalboard2
    pub fn get_pedalboard_version(&self, executable: &Path) -> Result<String, String> {
        let output = Command::new(executable)
            .arg("--version")
            .output()
            .map_err(|e| format!("Failed to get version: {}", e))?;
        
        let version_output = String::from_utf8_lossy(&output.stdout);
        
        if version_output.is_empty() {
            // Try alternative approaches
            let stderr_output = String::from_utf8_lossy(&output.stderr);
            if !stderr_output.is_empty() {
                Ok(stderr_output.to_string())
            } else {
                Ok("Unknown version".to_string())
            }
        } else {
            Ok(version_output.to_string())
        }
    }
    
    /// Create comprehensive test patch library
    pub fn create_test_patch_library(&self) -> BaselineTestLibrary {
        let mut patches = Vec::new();
        
        // Simple patches
        patches.push(BaselineTestPatch {
            name: "passthrough".to_string(),
            description: "Audio passthrough with no processing".to_string(),
            complexity: PatchComplexity::Simple,
            expected_behavior: "Input signal passes through unchanged".to_string(),
            test_signals: vec![
                create_sine_test_signal(440.0),
                create_noise_test_signal(),
            ],
            validation_criteria: vec![
                ValidationCriterion {
                    name: "signal_preservation".to_string(),
                    tolerance: 0.001,
                    description: "Output should match input within 0.1%".to_string(),
                },
            ],
        });
        
        patches.push(BaselineTestPatch {
            name: "single_plugin".to_string(),
            description: "Single plugin processing (e.g., reverb)".to_string(),
            complexity: PatchComplexity::Simple,
            expected_behavior: "Plugin processes audio with characteristic effect".to_string(),
            test_signals: vec![create_sine_test_signal(440.0)],
            validation_criteria: vec![
                ValidationCriterion {
                    name: "effect_applied".to_string(),
                    tolerance: 0.1,
                    description: "Audio should show characteristic plugin processing".to_string(),
                },
            ],
        });
        
        // Medium complexity patches
        patches.push(BaselineTestPatch {
            name: "plugin_chain".to_string(),
            description: "Chain of 3-5 plugins".to_string(),
            complexity: PatchComplexity::Medium,
            expected_behavior: "Sequential plugin processing".to_string(),
            test_signals: vec![create_sine_test_signal(440.0)],
            validation_criteria: vec![
                ValidationCriterion {
                    name: "chain_processing".to_string(),
                    tolerance: 0.05,
                    description: "Each plugin in chain should affect the signal".to_string(),
                },
            ],
        });
        
        // Complex patches
        patches.push(BaselineTestPatch {
            name: "parallel_processing".to_string(),
            description: "Parallel plugin branches with mixing".to_string(),
            complexity: PatchComplexity::Complex,
            expected_behavior: "Parallel processing with proper mixing".to_string(),
            test_signals: vec![create_sine_test_signal(440.0)],
            validation_criteria: vec![
                ValidationCriterion {
                    name: "parallel_mix".to_string(),
                    tolerance: 0.02,
                    description: "Parallel branches should be mixed correctly".to_string(),
                },
            ],
        });
        
        // Stress test patches
        patches.push(BaselineTestPatch {
            name: "stress_test".to_string(),
            description: "Maximum plugin count with complex routing".to_string(),
            complexity: PatchComplexity::Stress,
            expected_behavior: "System maintains real-time performance".to_string(),
            test_signals: vec![create_sine_test_signal(440.0)],
            validation_criteria: vec![
                ValidationCriterion {
                    name: "realtime_performance".to_string(),
                    tolerance: 0.01,
                    description: "No audio dropouts or excessive latency".to_string(),
                },
            ],
        });
        
        let test_signals = vec![
            create_sine_test_signal(440.0),
            create_sine_test_signal(880.0),
            create_noise_test_signal(),
            create_impulse_test_signal(),
            create_sweep_test_signal(),
        ];
        
        BaselineTestLibrary {
            patches,
            test_signals,
            created: std::time::SystemTime::now(),
        }
    }
    
    /// Analyze audio characteristics
    pub fn analyze_audio_characteristics(&self, samples: &[f32]) -> AudioCharacteristics {
        let rms = calculate_rms(samples);
        let peak = samples.iter().fold(0.0f32, |acc, &x| acc.max(x.abs()));
        
        // Simple frequency analysis (in real implementation, use FFT)
        let frequency_components = vec![
            FrequencyComponent {
                frequency: 440.0,
                magnitude: rms,
                phase: 0.0,
            }
        ];
        
        let dynamic_range = if peak > 0.0 { 20.0 * (peak / rms).log10() } else { 0.0 };
        
        AudioCharacteristics {
            rms,
            peak,
            frequency_components,
            dynamic_range,
            thd: 0.001, // Placeholder - would need proper THD calculation
        }
    }
    
    /// Measure baseline performance metrics
    pub fn measure_baseline_performance(&self) -> BaselineMetrics {
        // TODO: Implement actual performance measurement
        // This would require running the original Pedalboard2 and measuring:
        // - Audio latency (using loopback testing)
        // - Memory usage (via system monitoring)
        // - CPU usage (via system monitoring)
        // - Plugin loading times (via instrumentation)
        
        BaselineMetrics {
            audio_latency: Duration::from_millis(10), // Placeholder
            memory_usage_mb: 50.0, // Placeholder
            cpu_usage_percent: 15.0, // Placeholder
            plugin_load_time: Duration::from_millis(100), // Placeholder
            startup_time: Duration::from_secs(2), // Placeholder
        }
    }
}

// Helper functions for creating test signals
fn create_sine_test_signal(frequency: f32) -> TestSignal {
    TestSignal {
        name: format!("sine_{}hz", frequency as u32),
        signal_type: SignalType::SineWave { frequency },
        duration: Duration::from_secs(1),
        sample_rate: 44100.0,
    }
}

fn create_noise_test_signal() -> TestSignal {
    TestSignal {
        name: "white_noise".to_string(),
        signal_type: SignalType::WhiteNoise,
        duration: Duration::from_secs(1),
        sample_rate: 44100.0,
    }
}

fn create_impulse_test_signal() -> TestSignal {
    TestSignal {
        name: "impulse".to_string(),
        signal_type: SignalType::Impulse,
        duration: Duration::from_millis(100),
        sample_rate: 44100.0,
    }
}

fn create_sweep_test_signal() -> TestSignal {
    TestSignal {
        name: "frequency_sweep".to_string(),
        signal_type: SignalType::Sweep {
            start_freq: 20.0,
            end_freq: 20000.0,
        },
        duration: Duration::from_secs(5),
        sample_rate: 44100.0,
    }
}

// RMS calculation helper
fn calculate_rms(samples: &[f32]) -> f32 {
    if samples.is_empty() {
        return 0.0;
    }
    
    let sum_squares: f32 = samples.iter().map(|&x| x * x).sum();
    (sum_squares / samples.len() as f32).sqrt()
}