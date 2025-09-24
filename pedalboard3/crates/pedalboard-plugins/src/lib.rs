//! Pedalboard3 Plugin System
//!
//! VST/AU/CLAP plugin hosting with sandboxing and safety

use anyhow::{Context, Result};
use pedalboard_audio::{AudioBuffer, AudioNode, Sample};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use tracing::{debug, error, info, warn};
use uuid::Uuid;

pub mod host;
pub mod scanner;
pub mod vst;
pub mod safety;

pub use host::*;
pub use scanner::*;
pub use vst::*;
pub use safety::*;

/// Plugin format types
#[derive(Debug, Clone, PartialEq, Eq, Hash, serde::Serialize, serde::Deserialize)]
pub enum PluginFormat {
    VST2,
    VST3,
    AU,
    CLAP,
    LV2,
}

/// Plugin metadata
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct PluginInfo {
    pub id: Uuid,
    pub name: String,
    pub vendor: String,
    pub version: String,
    pub format: PluginFormat,
    pub path: PathBuf,
    pub category: String,
    pub input_channels: u32,
    pub output_channels: u32,
    pub has_editor: bool,
    pub can_receive_midi: bool,
    pub can_send_midi: bool,
    pub bypass_supported: bool,
}

impl PluginInfo {
    pub fn new(name: String, path: PathBuf, format: PluginFormat) -> Self {
        Self {
            id: Uuid::new_v4(),
            name,
            vendor: "Unknown".to_string(),
            version: "1.0".to_string(),
            format,
            path,
            category: "Effect".to_string(),
            input_channels: 2,
            output_channels: 2,
            has_editor: false,
            can_receive_midi: false,
            can_send_midi: false,
            bypass_supported: true,
        }
    }
}

/// Plugin instance state
#[derive(Debug, Clone, PartialEq)]
pub enum PluginState {
    Unloaded,
    Loading,
    Ready,
    Processing,
    Error(String),
    Bypassed,
}

/// Main plugin manager
pub struct PluginManager {
    plugins: HashMap<Uuid, PluginInfo>,
    instances: HashMap<Uuid, Box<dyn PluginInstance>>,
    scanner: PluginScanner,
    safety_manager: SafetyManager,
}

impl PluginManager {
    /// Create a new plugin manager
    pub fn new() -> Result<Self> {
        Ok(Self {
            plugins: HashMap::new(),
            instances: HashMap::new(),
            scanner: PluginScanner::new(),
            safety_manager: SafetyManager::new(),
        })
    }

    /// Scan for plugins in standard directories
    pub fn scan_plugins(&mut self) -> Result<usize> {
        info!("Scanning for plugins...");
        
        let found_plugins = self.scanner.scan_all()?;
        let count = found_plugins.len();
        
        for plugin_info in found_plugins {
            self.plugins.insert(plugin_info.id, plugin_info);
        }
        
        info!("Found {} plugins", count);
        Ok(count)
    }

    /// Get all available plugins
    pub fn get_available_plugins(&self) -> Vec<&PluginInfo> {
        self.plugins.values().collect()
    }

    /// Get plugins by format
    pub fn get_plugins_by_format(&self, format: PluginFormat) -> Vec<&PluginInfo> {
        self.plugins.values()
            .filter(|p| p.format == format)
            .collect()
    }

    /// Load a plugin instance
    pub fn load_plugin(&mut self, plugin_id: Uuid, instance_id: Uuid) -> Result<()> {
        let plugin_info = self.plugins.get(&plugin_id)
            .ok_or_else(|| anyhow::anyhow!("Plugin not found: {}", plugin_id))?;
        
        debug!("Loading plugin: {} ({})", plugin_info.name, plugin_info.format.format_name());
        
        // Create plugin instance based on format
        let instance: Box<dyn PluginInstance> = match plugin_info.format {
            PluginFormat::VST2 => {
                Box::new(VstPluginInstance::load(&plugin_info.path)?)
            },
            PluginFormat::VST3 => {
                return Err(anyhow::anyhow!("VST3 support not yet implemented"));
            },
            PluginFormat::AU => {
                return Err(anyhow::anyhow!("AU support not yet implemented"));
            },
            PluginFormat::CLAP => {
                return Err(anyhow::anyhow!("CLAP support not yet implemented"));
            },
            PluginFormat::LV2 => {
                return Err(anyhow::anyhow!("LV2 support not yet implemented"));
            },
        };

        self.instances.insert(instance_id, instance);
        info!("Plugin loaded: {} with instance ID {}", plugin_info.name, instance_id);
        Ok(())
    }

    /// Unload a plugin instance
    pub fn unload_plugin(&mut self, instance_id: Uuid) -> Result<()> {
        if let Some(mut instance) = self.instances.remove(&instance_id) {
            instance.deactivate()?;
            info!("Plugin instance unloaded: {}", instance_id);
        }
        Ok(())
    }

    /// Get plugin instance
    pub fn get_instance(&mut self, instance_id: Uuid) -> Option<&mut Box<dyn PluginInstance>> {
        self.instances.get_mut(&instance_id)
    }

    /// Process audio through a plugin instance
    pub fn process_plugin(&mut self, instance_id: Uuid, input: &AudioBuffer, output: &mut AudioBuffer) -> Result<()> {
        if let Some(instance) = self.instances.get_mut(&instance_id) {
            // Apply safety checks
            if !self.safety_manager.is_safe_to_process(instance_id)? {
                warn!("Plugin {} deemed unsafe, bypassing", instance_id);
                // Copy input to output (bypass)
                for (out_ch, in_ch) in output.iter_mut().zip(input.iter()) {
                    out_ch.copy_from_slice(in_ch);
                }
                return Ok(());
            }

            instance.process(input, output)
        } else {
            Err(anyhow::anyhow!("Plugin instance not found: {}", instance_id))
        }
    }

    /// Get number of loaded instances
    pub fn instance_count(&self) -> usize {
        self.instances.len()
    }

    /// Get number of available plugins
    pub fn plugin_count(&self) -> usize {
        self.plugins.len()
    }
}

impl Default for PluginManager {
    fn default() -> Self {
        Self::new().expect("Failed to create plugin manager")
    }
}

/// Plugin instance trait
pub trait PluginInstance: Send {
    fn activate(&mut self, sample_rate: f32, max_buffer_size: u32) -> Result<()>;
    fn deactivate(&mut self) -> Result<()>;
    fn process(&mut self, input: &AudioBuffer, output: &mut AudioBuffer) -> Result<()>;
    fn get_state(&self) -> PluginState;
    fn set_parameter(&mut self, index: u32, value: f32) -> Result<()>;
    fn get_parameter(&self, index: u32) -> Result<f32>;
    fn get_parameter_name(&self, index: u32) -> Result<String>;
    fn get_parameter_count(&self) -> u32;
    fn set_bypass(&mut self, bypassed: bool);
    fn is_bypassed(&self) -> bool;
}

impl PluginFormat {
    pub fn format_name(&self) -> &'static str {
        match self {
            PluginFormat::VST2 => "VST2",
            PluginFormat::VST3 => "VST3",
            PluginFormat::AU => "Audio Unit",
            PluginFormat::CLAP => "CLAP",
            PluginFormat::LV2 => "LV2",
        }
    }

    pub fn file_extensions(&self) -> &'static [&'static str] {
        match self {
            PluginFormat::VST2 => &[".dll", ".so", ".dylib", ".vst"],
            PluginFormat::VST3 => &[".vst3"],
            PluginFormat::AU => &[".component"],
            PluginFormat::CLAP => &[".clap"],
            PluginFormat::LV2 => &[".lv2"],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_plugin_manager_creation() {
        let manager = PluginManager::new();
        assert!(manager.is_ok());
        
        let manager = manager.unwrap();
        assert_eq!(manager.plugin_count(), 0);
        assert_eq!(manager.instance_count(), 0);
    }

    #[test]
    fn test_plugin_info_creation() {
        let info = PluginInfo::new(
            "Test Plugin".to_string(),
            PathBuf::from("/test/plugin.dll"),
            PluginFormat::VST2
        );
        
        assert_eq!(info.name, "Test Plugin");
        assert_eq!(info.format, PluginFormat::VST2);
        assert_eq!(info.input_channels, 2);
        assert_eq!(info.output_channels, 2);
    }

    #[test]
    fn test_plugin_format_extensions() {
        let vst2_exts = PluginFormat::VST2.file_extensions();
        assert!(vst2_exts.contains(&".dll"));
        assert!(vst2_exts.contains(&".so"));
        
        let vst3_exts = PluginFormat::VST3.file_extensions();
        assert!(vst3_exts.contains(&".vst3"));
    }

    #[test] 
    fn test_format_names() {
        assert_eq!(PluginFormat::VST2.format_name(), "VST2");
        assert_eq!(PluginFormat::VST3.format_name(), "VST3");
        assert_eq!(PluginFormat::AU.format_name(), "Audio Unit");
        assert_eq!(PluginFormat::CLAP.format_name(), "CLAP");
        assert_eq!(PluginFormat::LV2.format_name(), "LV2");
    }
}