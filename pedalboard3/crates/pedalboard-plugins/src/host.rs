//! Plugin host implementation for coordinating plugin instances

use crate::{PluginInstance, PluginManager, SafetyManager};
use anyhow::Result;
use pedalboard_audio::AudioBuffer;
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use tracing::{debug, info, warn};
use uuid::Uuid;

/// Plugin host configuration
#[derive(Debug, Clone)]
pub struct HostConfig {
    pub sample_rate: f32,
    pub buffer_size: u32,
    pub max_plugins: usize,
    pub enable_safety_checks: bool,
    pub parallel_processing: bool,
}

impl Default for HostConfig {
    fn default() -> Self {
        Self {
            sample_rate: 44100.0,
            buffer_size: 512,
            max_plugins: 64,
            enable_safety_checks: true,
            parallel_processing: false, // Conservative default for audio processing
        }
    }
}

/// Plugin host for managing multiple plugin instances
pub struct PluginHost {
    config: HostConfig,
    manager: PluginManager,
    active_plugins: HashMap<Uuid, PluginChainEntry>,
    processing_order: Vec<Uuid>,
}

/// Entry in the plugin processing chain
struct PluginChainEntry {
    instance_id: Uuid,
    plugin_id: Uuid,
    bypass: bool,
    input_gain: f32,
    output_gain: f32,
}

impl PluginHost {
    /// Create a new plugin host
    pub fn new(config: HostConfig) -> Result<Self> {
        let manager = PluginManager::new()?;
        
        Ok(Self {
            config,
            manager,
            active_plugins: HashMap::new(),
            processing_order: Vec::new(),
        })
    }

    /// Initialize the host
    pub fn initialize(&mut self) -> Result<()> {
        info!("Initializing plugin host with config: {:?}", self.config);
        
        // Scan for available plugins
        let plugin_count = self.manager.scan_plugins()?;
        info!("Plugin host initialized with {} available plugins", plugin_count);
        
        Ok(())
    }

    /// Add a plugin to the processing chain
    pub fn add_plugin(&mut self, plugin_id: Uuid) -> Result<Uuid> {
        if self.active_plugins.len() >= self.config.max_plugins {
            return Err(anyhow::anyhow!("Maximum plugin limit reached"));
        }

        let instance_id = Uuid::new_v4();
        
        // Load the plugin instance
        self.manager.load_plugin(plugin_id, instance_id)?;
        
        // Activate the plugin
        if let Some(instance) = self.manager.get_instance(instance_id) {
            instance.activate(self.config.sample_rate, self.config.buffer_size)?;
        }

        // Add to active plugins
        let entry = PluginChainEntry {
            instance_id,
            plugin_id,
            bypass: false,
            input_gain: 1.0,
            output_gain: 1.0,
        };

        self.active_plugins.insert(instance_id, entry);
        self.processing_order.push(instance_id);

        info!("Added plugin {} to processing chain", plugin_id);
        Ok(instance_id)
    }

    /// Remove a plugin from the processing chain
    pub fn remove_plugin(&mut self, instance_id: Uuid) -> Result<()> {
        if let Some(_entry) = self.active_plugins.remove(&instance_id) {
            // Remove from processing order
            self.processing_order.retain(|&id| id != instance_id);
            
            // Unload the plugin instance
            self.manager.unload_plugin(instance_id)?;
            
            info!("Removed plugin instance {} from processing chain", instance_id);
        }

        Ok(())
    }

    /// Set plugin bypass state
    pub fn set_plugin_bypass(&mut self, instance_id: Uuid, bypass: bool) -> Result<()> {
        if let Some(entry) = self.active_plugins.get_mut(&instance_id) {
            entry.bypass = bypass;
            
            // Also set bypass on the plugin instance
            if let Some(instance) = self.manager.get_instance(instance_id) {
                instance.set_bypass(bypass);
            }
            
            debug!("Set plugin {} bypass: {}", instance_id, bypass);
        }

        Ok(())
    }

    /// Set plugin input gain
    pub fn set_input_gain(&mut self, instance_id: Uuid, gain: f32) -> Result<()> {
        if let Some(entry) = self.active_plugins.get_mut(&instance_id) {
            entry.input_gain = gain.clamp(0.0, 4.0); // Limit gain to reasonable range
            debug!("Set plugin {} input gain: {}", instance_id, entry.input_gain);
        }

        Ok(())
    }

    /// Set plugin output gain
    pub fn set_output_gain(&mut self, instance_id: Uuid, gain: f32) -> Result<()> {
        if let Some(entry) = self.active_plugins.get_mut(&instance_id) {
            entry.output_gain = gain.clamp(0.0, 4.0); // Limit gain to reasonable range
            debug!("Set plugin {} output gain: {}", instance_id, entry.output_gain);
        }

        Ok(())
    }

    /// Reorder plugins in the processing chain
    pub fn reorder_plugins(&mut self, new_order: Vec<Uuid>) -> Result<()> {
        // Validate that all IDs in new_order exist in active_plugins
        for &id in &new_order {
            if !self.active_plugins.contains_key(&id) {
                return Err(anyhow::anyhow!("Plugin instance {} not found", id));
            }
        }

        // Check that we have all plugins (no missing, no extras)
        if new_order.len() != self.active_plugins.len() {
            return Err(anyhow::anyhow!("New order must contain all active plugins"));
        }

        self.processing_order = new_order;
        info!("Reordered plugins in processing chain");
        Ok(())
    }

    /// Process audio through the plugin chain
    pub fn process(&mut self, input: &AudioBuffer, output: &mut AudioBuffer) -> Result<()> {
        if self.active_plugins.is_empty() {
            // No plugins, just copy input to output
            self.copy_buffer(input, output);
            return Ok(());
        }

        // Create working buffers
        let mut current_buffer = input.clone();
        let mut next_buffer = vec![vec![0.0; input[0].len()]; input.len()];

        // Process plugins in order
        for &instance_id in &self.processing_order {
            if let Some(entry) = self.active_plugins.get(&instance_id) {
                if entry.bypass {
                    continue; // Skip bypassed plugins
                }

                // Apply input gain
                if entry.input_gain != 1.0 {
                    self.apply_gain(&mut current_buffer, entry.input_gain);
                }

                // Clear next buffer
                for channel in &mut next_buffer {
                    channel.fill(0.0);
                }

                // Process through plugin
                if let Err(e) = self.manager.process_plugin(instance_id, &current_buffer, &mut next_buffer) {
                    warn!("Plugin {} failed to process: {}", instance_id, e);
                    // On error, bypass this plugin
                    self.copy_buffer(&current_buffer, &mut next_buffer);
                }

                // Apply output gain
                if entry.output_gain != 1.0 {
                    self.apply_gain(&mut next_buffer, entry.output_gain);
                }

                // Swap buffers for next iteration
                std::mem::swap(&mut current_buffer, &mut next_buffer);
            }
        }

        // Copy final result to output
        self.copy_buffer(&current_buffer, output);
        Ok(())
    }

    /// Helper function to copy audio buffer
    fn copy_buffer(&self, src: &AudioBuffer, dst: &mut AudioBuffer) {
        let channels = src.len().min(dst.len());
        for ch in 0..channels {
            let samples = src[ch].len().min(dst[ch].len());
            dst[ch][..samples].copy_from_slice(&src[ch][..samples]);
        }
    }

    /// Helper function to apply gain to audio buffer
    fn apply_gain(&self, buffer: &mut AudioBuffer, gain: f32) {
        for channel in buffer {
            for sample in channel {
                *sample *= gain;
            }
        }
    }

    /// Get list of active plugin instances
    pub fn get_active_plugins(&self) -> Vec<Uuid> {
        self.processing_order.clone()
    }

    /// Get plugin processing order
    pub fn get_processing_order(&self) -> &[Uuid] {
        &self.processing_order
    }

    /// Get plugin manager reference
    pub fn get_manager(&self) -> &PluginManager {
        &self.manager
    }

    /// Get mutable plugin manager reference
    pub fn get_manager_mut(&mut self) -> &mut PluginManager {
        &mut self.manager
    }

    /// Get host configuration
    pub fn get_config(&self) -> &HostConfig {
        &self.config
    }

    /// Update host configuration
    pub fn update_config(&mut self, new_config: HostConfig) -> Result<()> {
        let old_config = self.config.clone();
        self.config = new_config;

        // If sample rate or buffer size changed, reactivate all plugins
        if old_config.sample_rate != self.config.sample_rate || 
           old_config.buffer_size != self.config.buffer_size {
            
            info!("Host config changed, reactivating plugins");
            
            for &instance_id in &self.processing_order {
                if let Some(instance) = self.manager.get_instance(instance_id) {
                    instance.deactivate()?;
                    instance.activate(self.config.sample_rate, self.config.buffer_size)?;
                }
            }
        }

        Ok(())
    }

    /// Get statistics about the host
    pub fn get_statistics(&self) -> HostStatistics {
        HostStatistics {
            active_plugin_count: self.active_plugins.len(),
            total_available_plugins: self.manager.plugin_count(),
            bypassed_plugins: self.active_plugins
                .values()
                .filter(|entry| entry.bypass)
                .count(),
        }
    }
}

/// Host statistics for monitoring
#[derive(Debug, Clone)]
pub struct HostStatistics {
    pub active_plugin_count: usize,
    pub total_available_plugins: usize,
    pub bypassed_plugins: usize,
}

impl Default for PluginHost {
    fn default() -> Self {
        Self::new(HostConfig::default()).expect("Failed to create default plugin host")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_host_creation() {
        let config = HostConfig::default();
        let host = PluginHost::new(config);
        assert!(host.is_ok());
    }

    #[test]
    fn test_host_config_default() {
        let config = HostConfig::default();
        assert_eq!(config.sample_rate, 44100.0);
        assert_eq!(config.buffer_size, 512);
        assert_eq!(config.max_plugins, 64);
        assert!(config.enable_safety_checks);
        assert!(!config.parallel_processing);
    }

    #[test]
    fn test_buffer_operations() {
        let host = PluginHost::default();
        
        // Test copy buffer
        let input = vec![vec![1.0, 2.0, 3.0], vec![4.0, 5.0, 6.0]];
        let mut output = vec![vec![0.0; 3]; 2];
        host.copy_buffer(&input, &mut output);
        assert_eq!(output, input);
        
        // Test apply gain
        let mut buffer = vec![vec![1.0, -1.0, 0.5]];
        host.apply_gain(&mut buffer, 2.0);
        assert_eq!(buffer[0], [2.0, -2.0, 1.0]);
    }

    #[test]
    fn test_statistics() {
        let host = PluginHost::default();
        let stats = host.get_statistics();
        assert_eq!(stats.active_plugin_count, 0);
        assert_eq!(stats.bypassed_plugins, 0);
    }
}