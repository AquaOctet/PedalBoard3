//! Plugin safety and sandboxing system

use anyhow::Result;
use std::collections::HashMap;
use std::time::{Duration, Instant};
use tracing::{debug, error, info, warn};
use uuid::Uuid;

/// Safety metrics for plugin monitoring
#[derive(Debug, Clone)]
pub struct SafetyMetrics {
    pub crash_count: u32,
    pub timeout_count: u32,
    pub excessive_cpu_count: u32,
    pub memory_leak_count: u32,
    pub last_crash: Option<Instant>,
    pub average_cpu_time: Duration,
    pub peak_memory_usage: usize,
    pub is_quarantined: bool,
}

impl SafetyMetrics {
    fn new() -> Self {
        Self {
            crash_count: 0,
            timeout_count: 0,
            excessive_cpu_count: 0,
            memory_leak_count: 0,
            last_crash: None,
            average_cpu_time: Duration::ZERO,
            peak_memory_usage: 0,
            is_quarantined: false,
        }
    }
}

/// Safety thresholds for plugin behavior
#[derive(Debug, Clone)]
pub struct SafetyThresholds {
    pub max_crashes: u32,
    pub max_timeouts: u32,
    pub max_cpu_time: Duration,
    pub max_memory_mb: usize,
    pub quarantine_duration: Duration,
}

impl Default for SafetyThresholds {
    fn default() -> Self {
        Self {
            max_crashes: 3,
            max_timeouts: 5,
            max_cpu_time: Duration::from_millis(50), // 50ms for real-time audio
            max_memory_mb: 100, // 100MB memory limit
            quarantine_duration: Duration::from_secs(300), // 5 minutes
        }
    }
}

/// Plugin safety manager
pub struct SafetyManager {
    metrics: HashMap<Uuid, SafetyMetrics>,
    thresholds: SafetyThresholds,
    start_times: HashMap<Uuid, Instant>,
}

impl SafetyManager {
    /// Create a new safety manager
    pub fn new() -> Self {
        Self {
            metrics: HashMap::new(),
            thresholds: SafetyThresholds::default(),
            start_times: HashMap::new(),
        }
    }

    /// Create with custom thresholds
    pub fn with_thresholds(thresholds: SafetyThresholds) -> Self {
        Self {
            metrics: HashMap::new(),
            thresholds,
            start_times: HashMap::new(),
        }
    }

    /// Check if a plugin is safe to process
    pub fn is_safe_to_process(&mut self, plugin_id: Uuid) -> Result<bool> {
        let metrics = self.metrics.entry(plugin_id).or_insert_with(SafetyMetrics::new);

        // Check if plugin is quarantined
        if metrics.is_quarantined {
            if let Some(last_crash) = metrics.last_crash {
                if last_crash.elapsed() < self.thresholds.quarantine_duration {
                    debug!("Plugin {} is quarantined", plugin_id);
                    return Ok(false);
                } else {
                    // Quarantine expired, allow processing but reset some metrics
                    metrics.is_quarantined = false;
                    metrics.crash_count = 0;
                    info!("Plugin {} quarantine expired, allowing processing", plugin_id);
                }
            }
        }

        // Check various safety conditions
        let is_safe = metrics.crash_count < self.thresholds.max_crashes
            && metrics.timeout_count < self.thresholds.max_timeouts
            && !metrics.is_quarantined;

        if !is_safe {
            debug!(
                "Plugin {} deemed unsafe: crashes={}, timeouts={}, quarantined={}",
                plugin_id, metrics.crash_count, metrics.timeout_count, metrics.is_quarantined
            );
        }

        Ok(is_safe)
    }

    /// Record the start of plugin processing
    pub fn start_processing(&mut self, plugin_id: Uuid) {
        self.start_times.insert(plugin_id, Instant::now());
    }

    /// Record successful plugin processing completion
    pub fn end_processing(&mut self, plugin_id: Uuid) -> Result<()> {
        if let Some(start_time) = self.start_times.remove(&plugin_id) {
            let processing_time = start_time.elapsed();
            
            let metrics = self.metrics.entry(plugin_id).or_insert_with(SafetyMetrics::new);
            
            // Update average CPU time (simple moving average)
            if metrics.average_cpu_time == Duration::ZERO {
                metrics.average_cpu_time = processing_time;
            } else {
                let avg_nanos = metrics.average_cpu_time.as_nanos() as f64;
                let new_nanos = processing_time.as_nanos() as f64;
                let updated_avg = (avg_nanos * 0.9) + (new_nanos * 0.1);
                metrics.average_cpu_time = Duration::from_nanos(updated_avg as u64);
            }

            // Check for excessive CPU usage
            if processing_time > self.thresholds.max_cpu_time {
                metrics.excessive_cpu_count += 1;
                warn!(
                    "Plugin {} took {}ms to process (limit: {}ms)",
                    plugin_id,
                    processing_time.as_millis(),
                    self.thresholds.max_cpu_time.as_millis()
                );
                
                // Quarantine if too many excessive CPU events
                if metrics.excessive_cpu_count > 10 {
                    self.quarantine_plugin(plugin_id, "Excessive CPU usage")?;
                }
            }
        }

        Ok(())
    }

    /// Record a plugin crash
    pub fn record_crash(&mut self, plugin_id: Uuid, error_message: &str) -> Result<()> {
        let metrics = self.metrics.entry(plugin_id).or_insert_with(SafetyMetrics::new);
        
        metrics.crash_count += 1;
        metrics.last_crash = Some(Instant::now());

        error!("Plugin {} crashed: {}", plugin_id, error_message);

        // Quarantine if crash limit exceeded
        if metrics.crash_count >= self.thresholds.max_crashes {
            self.quarantine_plugin(plugin_id, "Too many crashes")?;
        }

        Ok(())
    }

    /// Record a plugin timeout
    pub fn record_timeout(&mut self, plugin_id: Uuid) -> Result<()> {
        let metrics = self.metrics.entry(plugin_id).or_insert_with(SafetyMetrics::new);
        
        metrics.timeout_count += 1;
        warn!("Plugin {} timed out", plugin_id);

        // Quarantine if timeout limit exceeded
        if metrics.timeout_count >= self.thresholds.max_timeouts {
            self.quarantine_plugin(plugin_id, "Too many timeouts")?;
        }

        Ok(())
    }

    /// Record memory usage for a plugin
    pub fn record_memory_usage(&mut self, plugin_id: Uuid, memory_bytes: usize) -> Result<()> {
        let metrics = self.metrics.entry(plugin_id).or_insert_with(SafetyMetrics::new);
        
        if memory_bytes > metrics.peak_memory_usage {
            metrics.peak_memory_usage = memory_bytes;
        }

        // Check for excessive memory usage
        let memory_mb = memory_bytes / (1024 * 1024);
        if memory_mb > self.thresholds.max_memory_mb {
            metrics.memory_leak_count += 1;
            warn!(
                "Plugin {} using {}MB memory (limit: {}MB)",
                plugin_id, memory_mb, self.thresholds.max_memory_mb
            );

            // Quarantine if too many memory issues
            if metrics.memory_leak_count > 5 {
                self.quarantine_plugin(plugin_id, "Excessive memory usage")?;
            }
        }

        Ok(())
    }

    /// Manually quarantine a plugin
    pub fn quarantine_plugin(&mut self, plugin_id: Uuid, reason: &str) -> Result<()> {
        let metrics = self.metrics.entry(plugin_id).or_insert_with(SafetyMetrics::new);
        
        metrics.is_quarantined = true;
        metrics.last_crash = Some(Instant::now());
        
        error!("Plugin {} quarantined: {}", plugin_id, reason);
        Ok(())
    }

    /// Manually unquarantine a plugin (admin override)
    pub fn unquarantine_plugin(&mut self, plugin_id: Uuid) -> Result<()> {
        if let Some(metrics) = self.metrics.get_mut(&plugin_id) {
            metrics.is_quarantined = false;
            metrics.crash_count = 0;
            metrics.timeout_count = 0;
            metrics.excessive_cpu_count = 0;
            metrics.memory_leak_count = 0;
            
            info!("Plugin {} unquarantined (manual override)", plugin_id);
        }
        
        Ok(())
    }

    /// Get safety metrics for a plugin
    pub fn get_metrics(&self, plugin_id: Uuid) -> Option<&SafetyMetrics> {
        self.metrics.get(&plugin_id)
    }

    /// Get all plugin metrics
    pub fn get_all_metrics(&self) -> &HashMap<Uuid, SafetyMetrics> {
        &self.metrics
    }

    /// Get plugins that are currently quarantined
    pub fn get_quarantined_plugins(&self) -> Vec<Uuid> {
        self.metrics
            .iter()
            .filter(|(_, metrics)| metrics.is_quarantined)
            .map(|(&id, _)| id)
            .collect()
    }

    /// Clear metrics for a plugin (useful when plugin is unloaded)
    pub fn clear_metrics(&mut self, plugin_id: Uuid) {
        self.metrics.remove(&plugin_id);
        self.start_times.remove(&plugin_id);
        debug!("Cleared safety metrics for plugin {}", plugin_id);
    }

    /// Update safety thresholds
    pub fn update_thresholds(&mut self, new_thresholds: SafetyThresholds) {
        self.thresholds = new_thresholds;
        debug!("Updated safety thresholds: {:?}", self.thresholds);
    }

    /// Get current safety thresholds
    pub fn get_thresholds(&self) -> &SafetyThresholds {
        &self.thresholds
    }

    /// Reset all metrics (useful for testing or clean slate)
    pub fn reset_all_metrics(&mut self) {
        self.metrics.clear();
        self.start_times.clear();
        info!("All plugin safety metrics reset");
    }
}

impl Default for SafetyManager {
    fn default() -> Self {
        Self::new()
    }
}

/// RAII wrapper for automatic processing time tracking
pub struct ProcessingGuard<'a> {
    safety_manager: &'a mut SafetyManager,
    plugin_id: Uuid,
}

impl<'a> ProcessingGuard<'a> {
    /// Create a new processing guard
    pub fn new(safety_manager: &'a mut SafetyManager, plugin_id: Uuid) -> Self {
        safety_manager.start_processing(plugin_id);
        Self {
            safety_manager,
            plugin_id,
        }
    }
}

impl<'a> Drop for ProcessingGuard<'a> {
    fn drop(&mut self) {
        if let Err(e) = self.safety_manager.end_processing(self.plugin_id) {
            error!("Failed to record processing end for plugin {}: {}", self.plugin_id, e);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_safety_manager_creation() {
        let manager = SafetyManager::new();
        assert_eq!(manager.metrics.len(), 0);
    }

    #[test]
    fn test_initial_safety_check() {
        let mut manager = SafetyManager::new();
        let plugin_id = Uuid::new_v4();
        
        let is_safe = manager.is_safe_to_process(plugin_id).unwrap();
        assert!(is_safe);
    }

    #[test]
    fn test_crash_recording() {
        let mut manager = SafetyManager::new();
        let plugin_id = Uuid::new_v4();
        
        // Record multiple crashes
        for i in 1..=3 {
            manager.record_crash(plugin_id, &format!("Test crash {}", i)).unwrap();
        }
        
        // Should be quarantined after 3 crashes (default threshold)
        let is_safe = manager.is_safe_to_process(plugin_id).unwrap();
        assert!(!is_safe);
        
        let metrics = manager.get_metrics(plugin_id).unwrap();
        assert!(metrics.is_quarantined);
        assert_eq!(metrics.crash_count, 3);
    }

    #[test]
    fn test_timeout_recording() {
        let mut manager = SafetyManager::new();
        let plugin_id = Uuid::new_v4();
        
        // Record multiple timeouts
        for _ in 1..=5 {
            manager.record_timeout(plugin_id).unwrap();
        }
        
        // Should be quarantined after 5 timeouts (default threshold)
        let is_safe = manager.is_safe_to_process(plugin_id).unwrap();
        assert!(!is_safe);
    }

    #[test]
    fn test_processing_guard() {
        let mut manager = SafetyManager::new();
        let plugin_id = Uuid::new_v4();
        
        {
            let _guard = ProcessingGuard::new(&mut manager, plugin_id);
            // Guard automatically tracks start/end
        }
        
        // Should have recorded processing time
        let metrics = manager.get_metrics(plugin_id);
        assert!(metrics.is_some());
    }

    #[test]
    fn test_quarantine_and_unquarantine() {
        let mut manager = SafetyManager::new();
        let plugin_id = Uuid::new_v4();
        
        // Manually quarantine
        manager.quarantine_plugin(plugin_id, "Test reason").unwrap();
        let is_safe = manager.is_safe_to_process(plugin_id).unwrap();
        assert!(!is_safe);
        
        // Unquarantine
        manager.unquarantine_plugin(plugin_id).unwrap();
        let is_safe = manager.is_safe_to_process(plugin_id).unwrap();
        assert!(is_safe);
    }

    #[test]
    fn test_memory_tracking() {
        let mut manager = SafetyManager::new();
        let plugin_id = Uuid::new_v4();
        
        // Record normal memory usage
        manager.record_memory_usage(plugin_id, 50 * 1024 * 1024).unwrap(); // 50MB
        let is_safe = manager.is_safe_to_process(plugin_id).unwrap();
        assert!(is_safe);
        
        // Record excessive memory usage multiple times
        for _ in 0..6 {
            manager.record_memory_usage(plugin_id, 150 * 1024 * 1024).unwrap(); // 150MB
        }
        
        let is_safe = manager.is_safe_to_process(plugin_id).unwrap();
        assert!(!is_safe); // Should be quarantined
    }
}