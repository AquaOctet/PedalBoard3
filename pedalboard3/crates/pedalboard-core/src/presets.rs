//! Preset management for individual plugins and effect chains

use anyhow::Result;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use uuid::Uuid;

/// Individual plugin preset
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginPreset {
    pub name: String,
    pub plugin_id: Uuid,
    pub plugin_name: String,
    pub parameters: HashMap<u32, f32>,
    pub created: chrono::DateTime<chrono::Utc>,
    pub modified: chrono::DateTime<chrono::Utc>,
    pub tags: Vec<String>,
    pub author: Option<String>,
    pub description: Option<String>,
}

/// Chain preset containing multiple plugins
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ChainPreset {
    pub name: String,
    pub plugins: Vec<ChainPluginEntry>,
    pub created: chrono::DateTime<chrono::Utc>,
    pub modified: chrono::DateTime<chrono::Utc>,
    pub tags: Vec<String>,
    pub author: Option<String>,
    pub description: Option<String>,
}

/// Plugin entry in a chain preset
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ChainPluginEntry {
    pub plugin_id: Uuid,
    pub plugin_name: String,
    pub parameters: HashMap<u32, f32>,
    pub bypass: bool,
    pub input_gain: f32,
    pub output_gain: f32,
    pub position: usize,
}

impl PluginPreset {
    /// Create a new plugin preset
    pub fn new(name: String, plugin_id: Uuid, plugin_name: String) -> Self {
        let now = chrono::Utc::now();
        
        Self {
            name,
            plugin_id,
            plugin_name,
            parameters: HashMap::new(),
            created: now,
            modified: now,
            tags: Vec::new(),
            author: None,
            description: None,
        }
    }

    /// Set parameter value
    pub fn set_parameter(&mut self, index: u32, value: f32) {
        self.parameters.insert(index, value.clamp(0.0, 1.0));
        self.modified = chrono::Utc::now();
    }

    /// Get parameter value
    pub fn get_parameter(&self, index: u32) -> Option<f32> {
        self.parameters.get(&index).copied()
    }

    /// Add tag
    pub fn add_tag(&mut self, tag: String) {
        if !self.tags.contains(&tag) {
            self.tags.push(tag);
            self.modified = chrono::Utc::now();
        }
    }

    /// Remove tag
    pub fn remove_tag(&mut self, tag: &str) {
        self.tags.retain(|t| t != tag);
        self.modified = chrono::Utc::now();
    }

    /// Set metadata
    pub fn set_author(&mut self, author: String) {
        self.author = Some(author);
        self.modified = chrono::Utc::now();
    }

    /// Set description
    pub fn set_description(&mut self, description: String) {
        self.description = Some(description);
        self.modified = chrono::Utc::now();
    }

    /// Save preset to file
    pub fn save<P: AsRef<Path>>(&self, path: P) -> Result<()> {
        let content = serde_json::to_string_pretty(self)?;
        fs::write(path.as_ref(), content)?;
        Ok(())
    }

    /// Load preset from file
    pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
        let content = fs::read_to_string(path.as_ref())?;
        let preset: PluginPreset = serde_json::from_str(&content)?;
        Ok(preset)
    }
}

impl ChainPreset {
    /// Create a new chain preset
    pub fn new(name: String) -> Self {
        let now = chrono::Utc::now();
        
        Self {
            name,
            plugins: Vec::new(),
            created: now,
            modified: now,
            tags: Vec::new(),
            author: None,
            description: None,
        }
    }

    /// Add plugin to chain
    pub fn add_plugin(&mut self, entry: ChainPluginEntry) {
        self.plugins.push(entry);
        self.modified = chrono::Utc::now();
    }

    /// Remove plugin from chain
    pub fn remove_plugin(&mut self, plugin_id: Uuid) {
        self.plugins.retain(|p| p.plugin_id != plugin_id);
        self.reorder_positions();
        self.modified = chrono::Utc::now();
    }

    /// Reorder plugins
    pub fn reorder_plugins(&mut self, new_order: Vec<Uuid>) -> Result<()> {
        let mut new_plugins = Vec::new();
        
        for (position, &plugin_id) in new_order.iter().enumerate() {
            if let Some(mut plugin) = self.plugins.iter().find(|p| p.plugin_id == plugin_id).cloned() {
                plugin.position = position;
                new_plugins.push(plugin);
            }
        }
        
        if new_plugins.len() != self.plugins.len() {
            return Err(anyhow::anyhow!("Chain reorder failed: count mismatch"));
        }
        
        self.plugins = new_plugins;
        self.modified = chrono::Utc::now();
        Ok(())
    }

    /// Get plugins in processing order
    pub fn get_ordered_plugins(&self) -> Vec<&ChainPluginEntry> {
        let mut plugins: Vec<&ChainPluginEntry> = self.plugins.iter().collect();
        plugins.sort_by_key(|p| p.position);
        plugins
    }

    /// Reorder positions after removal
    fn reorder_positions(&mut self) {
        for (i, plugin) in self.plugins.iter_mut().enumerate() {
            plugin.position = i;
        }
    }

    /// Add tag
    pub fn add_tag(&mut self, tag: String) {
        if !self.tags.contains(&tag) {
            self.tags.push(tag);
            self.modified = chrono::Utc::now();
        }
    }

    /// Set metadata
    pub fn set_author(&mut self, author: String) {
        self.author = Some(author);
        self.modified = chrono::Utc::now();
    }

    /// Set description
    pub fn set_description(&mut self, description: String) {
        self.description = Some(description);
        self.modified = chrono::Utc::now();
    }

    /// Save preset to file
    pub fn save<P: AsRef<Path>>(&self, path: P) -> Result<()> {
        let content = serde_json::to_string_pretty(self)?;
        fs::write(path.as_ref(), content)?;
        Ok(())
    }

    /// Load preset from file
    pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
        let content = fs::read_to_string(path.as_ref())?;
        let preset: ChainPreset = serde_json::from_str(&content)?;
        Ok(preset)
    }
}

impl ChainPluginEntry {
    /// Create a new chain plugin entry
    pub fn new(plugin_id: Uuid, plugin_name: String, position: usize) -> Self {
        Self {
            plugin_id,
            plugin_name,
            parameters: HashMap::new(),
            bypass: false,
            input_gain: 1.0,
            output_gain: 1.0,
            position,
        }
    }

    /// Set parameter
    pub fn set_parameter(&mut self, index: u32, value: f32) {
        self.parameters.insert(index, value.clamp(0.0, 1.0));
    }

    /// Get parameter
    pub fn get_parameter(&self, index: u32) -> Option<f32> {
        self.parameters.get(&index).copied()
    }
}

/// Preset manager for organizing and accessing presets
pub struct PresetManager {
    plugin_presets: HashMap<Uuid, Vec<PluginPreset>>, // Keyed by plugin_id
    chain_presets: Vec<ChainPreset>,
    preset_directory: PathBuf,
}

impl PresetManager {
    /// Create a new preset manager
    pub fn new(preset_directory: PathBuf) -> Result<Self> {
        if !preset_directory.exists() {
            fs::create_dir_all(&preset_directory)?;
        }

        let mut manager = Self {
            plugin_presets: HashMap::new(),
            chain_presets: Vec::new(),
            preset_directory,
        };

        manager.load_all_presets()?;
        Ok(manager)
    }

    /// Load all presets from directory
    fn load_all_presets(&mut self) -> Result<()> {
        if !self.preset_directory.exists() {
            return Ok(());
        }

        for entry in fs::read_dir(&self.preset_directory)? {
            let entry = entry?;
            let path = entry.path();

            if path.is_file() {
                if let Some(extension) = path.extension().and_then(|s| s.to_str()) {
                    match extension {
                        "pbpreset" => {
                            if let Ok(preset) = PluginPreset::load(&path) {
                                self.plugin_presets
                                    .entry(preset.plugin_id)
                                    .or_insert_with(Vec::new)
                                    .push(preset);
                            }
                        }
                        "pbchain" => {
                            if let Ok(preset) = ChainPreset::load(&path) {
                                self.chain_presets.push(preset);
                            }
                        }
                        _ => {} // Ignore other file types
                    }
                }
            }
        }

        Ok(())
    }

    /// Save plugin preset
    pub fn save_plugin_preset(&mut self, preset: PluginPreset) -> Result<()> {
        let filename = format!("{}.pbpreset", preset.name.replace(' ', "_"));
        let path = self.preset_directory.join(filename);
        preset.save(&path)?;

        // Add to in-memory collection
        self.plugin_presets
            .entry(preset.plugin_id)
            .or_insert_with(Vec::new)
            .push(preset);

        Ok(())
    }

    /// Save chain preset
    pub fn save_chain_preset(&mut self, preset: ChainPreset) -> Result<()> {
        let filename = format!("{}.pbchain", preset.name.replace(' ', "_"));
        let path = self.preset_directory.join(filename);
        preset.save(&path)?;

        // Add to in-memory collection
        self.chain_presets.push(preset);
        Ok(())
    }

    /// Get plugin presets for a specific plugin
    pub fn get_plugin_presets(&self, plugin_id: Uuid) -> Vec<&PluginPreset> {
        self.plugin_presets.get(&plugin_id).map(|v| v.iter().collect()).unwrap_or_default()
    }

    /// Get all chain presets
    pub fn get_chain_presets(&self) -> &[ChainPreset] {
        &self.chain_presets
    }

    /// Find plugin presets by name
    pub fn find_plugin_presets_by_name(&self, name: &str) -> Vec<&PluginPreset> {
        self.plugin_presets
            .values()
            .flatten()
            .filter(|preset| preset.name.to_lowercase().contains(&name.to_lowercase()))
            .collect()
    }

    /// Find chain presets by name
    pub fn find_chain_presets_by_name(&self, name: &str) -> Vec<&ChainPreset> {
        self.chain_presets
            .iter()
            .filter(|preset| preset.name.to_lowercase().contains(&name.to_lowercase()))
            .collect()
    }

    /// Find presets by tag
    pub fn find_plugin_presets_by_tag(&self, tag: &str) -> Vec<&PluginPreset> {
        self.plugin_presets
            .values()
            .flatten()
            .filter(|preset| preset.tags.contains(&tag.to_string()))
            .collect()
    }

    /// Find chain presets by tag
    pub fn find_chain_presets_by_tag(&self, tag: &str) -> Vec<&ChainPreset> {
        self.chain_presets
            .iter()
            .filter(|preset| preset.tags.contains(&tag.to_string()))
            .collect()
    }

    /// Delete plugin preset
    pub fn delete_plugin_preset(&mut self, plugin_id: Uuid, preset_name: &str) -> Result<()> {
        // Remove from in-memory collection
        if let Some(presets) = self.plugin_presets.get_mut(&plugin_id) {
            presets.retain(|p| p.name != preset_name);
        }

        // Remove file
        let filename = format!("{}.pbpreset", preset_name.replace(' ', "_"));
        let path = self.preset_directory.join(filename);
        if path.exists() {
            fs::remove_file(path)?;
        }

        Ok(())
    }

    /// Delete chain preset
    pub fn delete_chain_preset(&mut self, preset_name: &str) -> Result<()> {
        // Remove from in-memory collection
        self.chain_presets.retain(|p| p.name != preset_name);

        // Remove file
        let filename = format!("{}.pbchain", preset_name.replace(' ', "_"));
        let path = self.preset_directory.join(filename);
        if path.exists() {
            fs::remove_file(path)?;
        }

        Ok(())
    }

    /// Get all unique tags
    pub fn get_all_tags(&self) -> Vec<String> {
        let mut tags: Vec<String> = self.plugin_presets
            .values()
            .flatten()
            .flat_map(|p| p.tags.iter())
            .cloned()
            .collect();

        tags.extend(
            self.chain_presets
                .iter()
                .flat_map(|p| p.tags.iter())
                .cloned()
        );

        tags.sort();
        tags.dedup();
        tags
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn test_plugin_preset_creation() {
        let plugin_id = Uuid::new_v4();
        let mut preset = PluginPreset::new("Test Preset".to_string(), plugin_id, "Test Plugin".to_string());
        
        preset.set_parameter(0, 0.5);
        assert_eq!(preset.get_parameter(0), Some(0.5));
        
        preset.add_tag("Reverb".to_string());
        assert!(preset.tags.contains(&"Reverb".to_string()));
    }

    #[test]
    fn test_chain_preset_creation() {
        let mut chain = ChainPreset::new("Test Chain".to_string());
        
        let entry = ChainPluginEntry::new(Uuid::new_v4(), "Plugin 1".to_string(), 0);
        chain.add_plugin(entry);
        
        assert_eq!(chain.plugins.len(), 1);
        assert_eq!(chain.get_ordered_plugins().len(), 1);
    }

    #[test]
    fn test_preset_save_load() {
        let temp_dir = TempDir::new().unwrap();
        let preset_path = temp_dir.path().join("test.pbpreset");
        
        // Create and save preset
        let original_preset = PluginPreset::new("Test".to_string(), Uuid::new_v4(), "Plugin".to_string());
        original_preset.save(&preset_path).unwrap();
        
        // Load preset
        let loaded_preset = PluginPreset::load(&preset_path).unwrap();
        assert_eq!(loaded_preset.name, "Test");
    }

    #[test]
    fn test_preset_manager() {
        let temp_dir = TempDir::new().unwrap();
        let mut manager = PresetManager::new(temp_dir.path().to_path_buf()).unwrap();
        
        // Create and save plugin preset
        let plugin_id = Uuid::new_v4();
        let preset = PluginPreset::new("Test Preset".to_string(), plugin_id, "Test Plugin".to_string());
        manager.save_plugin_preset(preset).unwrap();
        
        // Verify preset exists
        let presets = manager.get_plugin_presets(plugin_id);
        assert_eq!(presets.len(), 1);
        assert_eq!(presets[0].name, "Test Preset");
    }

    #[test]
    fn test_preset_search() {
        let temp_dir = TempDir::new().unwrap();
        let mut manager = PresetManager::new(temp_dir.path().to_path_buf()).unwrap();
        
        // Create preset with tag
        let plugin_id = Uuid::new_v4();
        let mut preset = PluginPreset::new("Reverb Preset".to_string(), plugin_id, "Reverb Plugin".to_string());
        preset.add_tag("reverb".to_string());
        manager.save_plugin_preset(preset).unwrap();
        
        // Search by name
        let found = manager.find_plugin_presets_by_name("Reverb");
        assert_eq!(found.len(), 1);
        
        // Search by tag
        let found_by_tag = manager.find_plugin_presets_by_tag("reverb");
        assert_eq!(found_by_tag.len(), 1);
    }
}