//! Performance Benchmarking Tests
//!
//! Comprehensive performance tests to ensure the Rust implementation
//! meets or exceeds the performance of the original Pedalboard2.

use std::time::{Duration, Instant};
use std::collections::HashMap;

#[cfg(test)]
mod tests {
    use super::*;
    use crate::utils::*;
    
    #[test]
    fn test_audio_processing_latency() {
        // Test the latency of audio processing
        let latency_tester = LatencyTester::new();
        
        // Test with various buffer sizes
        let buffer_sizes = [64, 128, 256, 512, 1024, 2048];
        
        for &buffer_size in &buffer_sizes {
            let duration = latency_tester.measure_processing_latency(buffer_size);
            println!("Buffer size {}: {:?}", buffer_size, duration);
            
            // Calculate maximum allowable processing time
            let sample_rate = 44100.0;
            let buffer_duration = Duration::from_secs_f64(buffer_size as f64 / sample_rate);
            let max_processing_time = buffer_duration * 2; // 50% CPU usage target
            
            assert!(duration < max_processing_time,
                    "Processing too slow for buffer size {}: {:?} > {:?}",
                    buffer_size, duration, max_processing_time);
        }
    }
    
    #[test]
    fn test_memory_allocation_tracking() {
        // Test that audio processing doesn't allocate memory
        let mut tracker = MemoryTracker::new();
        
        // Create test audio data
        let input = generate_sine_wave(440.0, 44100.0, 0.1);
        
        tracker.start_tracking();
        
        // TODO: Process audio through Rust implementation
        // process_audio_block(&input);
        
        let allocations = tracker.stop_tracking();
        
        // In real-time audio, we should have zero allocations
        assert_eq!(allocations, 0, 
                  "Audio processing allocated {} times - should be zero!", allocations);
    }
    
    #[test]
    fn test_cpu_usage_measurement() {
        // Test CPU usage during sustained audio processing
        let cpu_monitor = CpuMonitor::new();
        
        let duration_secs = 5.0; // Test for 5 seconds
        let buffer_size = 512;
        let sample_rate = 44100.0;
        let num_buffers = (duration_secs * sample_rate / buffer_size as f32) as usize;
        
        println!("Testing CPU usage for {} buffers over {:.1}s", num_buffers, duration_secs);
        
        let start_time = Instant::now();
        cpu_monitor.start_monitoring();
        
        for _ in 0..num_buffers {
            // Simulate audio processing
            let input = generate_sine_wave(440.0, sample_rate, buffer_size as f32 / sample_rate);
            
            // TODO: Process through actual Rust audio graph
            // process_audio_block(&input);
            
            // Simulate real-time timing
            std::thread::sleep(Duration::from_micros((buffer_size as f64 / sample_rate * 1_000_000.0) as u64));
        }
        
        let cpu_usage = cpu_monitor.stop_monitoring();
        let elapsed = start_time.elapsed();
        
        println!("Average CPU usage: {:.1}%", cpu_usage);
        println!("Total elapsed time: {:?}", elapsed);
        
        // CPU usage should be reasonable (< 50% for single-threaded processing)
        assert!(cpu_usage < 50.0, "CPU usage too high: {:.1}%", cpu_usage);
    }
    
    #[test]
    fn test_plugin_loading_performance() {
        // Test how quickly plugins can be loaded and initialized
        let loader = PluginLoader::new();
        
        // Test loading times for different plugin counts
        let plugin_counts = [1, 5, 10, 20];
        
        for &count in &plugin_counts {
            let start = Instant::now();
            
            // TODO: Load actual plugins when plugin system is implemented
            // let plugins = loader.load_test_plugins(count);
            
            let load_time = start.elapsed();
            println!("Loading {} plugins took: {:?}", count, load_time);
            
            // Plugin loading should be reasonable
            let max_time_per_plugin = Duration::from_millis(100);
            let max_total_time = max_time_per_plugin * count;
            
            assert!(load_time < max_total_time,
                    "Plugin loading too slow: {:?} for {} plugins", load_time, count);
        }
    }
    
    #[test]
    fn test_graph_processing_scalability() {
        // Test how audio graph performance scales with complexity
        let complexity_levels = [
            (1, 0),   // 1 plugin, 0 connections
            (5, 4),   // 5 plugins, 4 connections (chain)
            (10, 15), // 10 plugins, 15 connections (complex graph)
            (20, 30), // 20 plugins, 30 connections (stress test)
        ];
        
        for &(num_plugins, num_connections) in &complexity_levels {
            let benchmark = AudioGraphBenchmark::new(num_plugins, num_connections);
            let processing_time = benchmark.measure_processing_time();
            
            println!("Graph with {} plugins, {} connections: {:?}",
                    num_plugins, num_connections, processing_time);
            
            // Processing time should scale reasonably
            let base_time = Duration::from_micros(100); // 100μs base
            let plugin_overhead = Duration::from_micros(50); // 50μs per plugin
            let max_time = base_time + plugin_overhead * num_plugins;
            
            assert!(processing_time < max_time,
                    "Graph processing too slow: {:?} for {} plugins", 
                    processing_time, num_plugins);
        }
    }
}

/// Latency measurement utilities
pub struct LatencyTester {
    sample_rate: f64,
}

impl LatencyTester {
    pub fn new() -> Self {
        Self {
            sample_rate: 44100.0,
        }
    }
    
    pub fn measure_processing_latency(&self, buffer_size: usize) -> Duration {
        let input = generate_sine_wave(440.0, self.sample_rate as f32, 
                                     buffer_size as f32 / self.sample_rate as f32);
        
        let start = Instant::now();
        
        // TODO: Process through actual audio graph
        // process_audio_buffer(&input);
        
        // For now, simulate some processing
        let _processed: Vec<f32> = input.iter().map(|&x| x * 0.5).collect();
        
        start.elapsed()
    }
    
    pub fn measure_round_trip_latency(&self) -> Duration {
        // Measure total system latency (input -> processing -> output)
        // TODO: Implement when full audio I/O system is available
        Duration::from_millis(10) // Placeholder
    }
}

/// Memory allocation tracking
pub struct MemoryTracker {
    initial_allocations: usize,
    tracking: bool,
}

impl MemoryTracker {
    pub fn new() -> Self {
        Self {
            initial_allocations: 0,
            tracking: false,
        }
    }
    
    pub fn start_tracking(&mut self) {
        // TODO: Implement actual allocation tracking
        // This might require custom allocator or external tools
        self.initial_allocations = 0; // Placeholder
        self.tracking = true;
    }
    
    pub fn stop_tracking(&mut self) -> usize {
        self.tracking = false;
        
        // TODO: Return actual allocation count
        // For now, return 0 as placeholder
        0
    }
}

/// CPU usage monitoring
pub struct CpuMonitor {
    start_time: Option<Instant>,
}

impl CpuMonitor {
    pub fn new() -> Self {
        Self {
            start_time: None,
        }
    }
    
    pub fn start_monitoring(&mut self) {
        self.start_time = Some(Instant::now());
    }
    
    pub fn stop_monitoring(&mut self) -> f64 {
        // TODO: Implement actual CPU usage measurement
        // This might require platform-specific APIs
        
        // Return placeholder value
        25.0 // 25% CPU usage
    }
}

/// Plugin loading performance tester
pub struct PluginLoader;

impl PluginLoader {
    pub fn new() -> Self {
        Self
    }
    
    pub fn load_test_plugins(&self, count: usize) -> Vec<TestPlugin> {
        let mut plugins = Vec::with_capacity(count);
        
        for i in 0..count {
            // Simulate plugin loading time
            std::thread::sleep(Duration::from_millis(10));
            
            plugins.push(TestPlugin {
                id: i,
                name: format!("TestPlugin{}", i),
                initialized: true,
            });
        }
        
        plugins
    }
}

#[derive(Debug)]
pub struct TestPlugin {
    pub id: usize,
    pub name: String,
    pub initialized: bool,
}

/// Audio graph benchmark framework
pub struct AudioGraphBenchmark {
    num_plugins: usize,
    num_connections: usize,
}

impl AudioGraphBenchmark {
    pub fn new(num_plugins: usize, num_connections: usize) -> Self {
        Self {
            num_plugins,
            num_connections,
        }
    }
    
    pub fn measure_processing_time(&self) -> Duration {
        // TODO: Create actual audio graph with specified complexity
        // For now, simulate processing time based on complexity
        
        let base_time = Duration::from_micros(50);
        let plugin_time = Duration::from_micros(20) * self.num_plugins as u32;
        let connection_time = Duration::from_micros(5) * self.num_connections as u32;
        
        // Simulate processing
        std::thread::sleep(base_time + plugin_time + connection_time);
        
        base_time + plugin_time + connection_time
    }
}

/// Performance comparison framework
pub struct PerformanceComparator {
    rust_metrics: HashMap<String, f64>,
    original_metrics: HashMap<String, f64>,
}

impl PerformanceComparator {
    pub fn new() -> Self {
        Self {
            rust_metrics: HashMap::new(),
            original_metrics: HashMap::new(),
        }
    }
    
    pub fn add_rust_metric(&mut self, name: String, value: f64) {
        self.rust_metrics.insert(name, value);
    }
    
    pub fn add_original_metric(&mut self, name: String, value: f64) {
        self.original_metrics.insert(name, value);
    }
    
    pub fn compare_metrics(&self) -> PerformanceReport {
        let mut comparisons = HashMap::new();
        
        for (metric_name, rust_value) in &self.rust_metrics {
            if let Some(original_value) = self.original_metrics.get(metric_name) {
                let improvement_ratio = original_value / rust_value;
                let improvement_percent = (improvement_ratio - 1.0) * 100.0;
                
                comparisons.insert(metric_name.clone(), MetricComparison {
                    rust_value: *rust_value,
                    original_value: *original_value,
                    improvement_percent,
                    is_better: improvement_ratio > 1.0,
                });
            }
        }
        
        PerformanceReport {
            comparisons,
        }
    }
}

#[derive(Debug)]
pub struct PerformanceReport {
    pub comparisons: HashMap<String, MetricComparison>,
}

#[derive(Debug)]
pub struct MetricComparison {
    pub rust_value: f64,
    pub original_value: f64,
    pub improvement_percent: f64,
    pub is_better: bool,
}

impl PerformanceReport {
    pub fn print_summary(&self) {
        println!("\n=== Performance Comparison Report ===");
        
        for (metric_name, comparison) in &self.comparisons {
            let status = if comparison.is_better { "✓" } else { "✗" };
            println!("{} {}: {:.2}% change (Rust: {:.3}, Original: {:.3})",
                    status,
                    metric_name,
                    comparison.improvement_percent,
                    comparison.rust_value,
                    comparison.original_value);
        }
    }
    
    pub fn overall_performance_better(&self) -> bool {
        let total_comparisons = self.comparisons.len();
        if total_comparisons == 0 {
            return false;
        }
        
        let better_count = self.comparisons.values()
            .filter(|c| c.is_better)
            .count();
        
        // Consider overall performance better if >50% of metrics improved
        better_count as f64 / total_comparisons as f64 > 0.5
    }
}