//! Application configuration management

use anyhow::Result;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use tracing::{debug, info, warn};

/// Main application configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ApplicationConfig {
    pub audio: AudioConfiguration,
    pub plugins: PluginConfiguration,
    pub ui: UIConfiguration,
    pub paths: PathConfiguration,
}

/// Audio-specific configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AudioConfiguration {
    pub sample_rate: u32,
    pub buffer_size: u32,
    pub channels: u32,
    pub input_device: Option<String>,
    pub output_device: Option<String>,
}

/// Plugin-specific configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginConfiguration {
    pub max_plugins: usize,
    pub enable_safety_checks: bool,
    pub parallel_processing: bool,
    pub scan_paths: Vec<PathBuf>,
    pub blacklisted_plugins: Vec<String>,
}

/// UI-specific configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UIConfiguration {
    pub theme: String,
    pub window_width: u32,
    pub window_height: u32,
    pub show_plugin_editor: bool,
    pub auto_save_sessions: bool,
}

/// Path configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PathConfiguration {
    pub config_dir: PathBuf,
    pub session_dir: PathBuf,
    pub preset_dir: PathBuf,
    pub log_dir: PathBuf,
}

impl Default for ApplicationConfig {
    fn default() -> Self {
        let config_dir = Self::default_config_dir();
        
        Self {
            audio: AudioConfiguration::default(),
            plugins: PluginConfiguration::default(),
            ui: UIConfiguration::default(),
            paths: PathConfiguration {
                config_dir: config_dir.clone(),
                session_dir: config_dir.join("sessions"),
                preset_dir: config_dir.join("presets"),
                log_dir: config_dir.join("logs"),
            },
        }
    }
}

impl Default for AudioConfiguration {
    fn default() -> Self {
        Self {
            sample_rate: 44100,
            buffer_size: 512,
            channels: 2,
            input_device: None,
            output_device: None,
        }
    }
}

impl Default for PluginConfiguration {
    fn default() -> Self {
        Self {
            max_plugins: 64,
            enable_safety_checks: true,
            parallel_processing: false,
            scan_paths: Vec::new(), // Will be populated by scanner
            blacklisted_plugins: Vec::new(),
        }
    }
}

impl Default for UIConfiguration {
    fn default() -> Self {
        Self {
            theme: "dark".to_string(),
            window_width: 1200,
            window_height: 800,
            show_plugin_editor: true,
            auto_save_sessions: true,
        }
    }
}

impl ApplicationConfig {
    /// Get the default configuration directory
    fn default_config_dir() -> PathBuf {
        if let Some(config_dir) = dirs::config_dir() {
            config_dir.join("Pedalboard3")
        } else {
            // Fallback for systems without XDG
            PathBuf::from(".pedalboard3")
        }
    }

    /// Get configuration file path
    pub fn config_file_path() -> PathBuf {
        Self::default_config_dir().join("config.toml")
    }

    /// Load configuration from file, or create default if not found
    pub fn load_or_default() -> Result<Self> {
        let config_path = Self::config_file_path();
        
        if config_path.exists() {
            info!("Loading configuration from: {}", config_path.display());
            Self::load(&config_path)
        } else {
            info!("Configuration file not found, creating default");
            let config = Self::default();
            config.save()?;
            Ok(config)
        }
    }

    /// Load configuration from file
    pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
        let content = fs::read_to_string(path.as_ref())?;
        let config: ApplicationConfig = toml::from_str(&content)?;
        debug!("Loaded configuration: {:?}", config);
        Ok(config)
    }

    /// Save configuration to file
    pub fn save(&self) -> Result<()> {
        let config_path = Self::config_file_path();
        self.save_to(&config_path)
    }

    /// Save configuration to specific path
    pub fn save_to<P: AsRef<Path>>(&self, path: P) -> Result<()> {
        // Ensure parent directory exists
        if let Some(parent) = path.as_ref().parent() {
            fs::create_dir_all(parent)?;
        }

        let content = toml::to_string_pretty(self)?;
        fs::write(path.as_ref(), content)?;
        info!("Configuration saved to: {}", path.as_ref().display());
        Ok(())
    }

    /// Ensure all configured directories exist
    pub fn ensure_directories(&self) -> Result<()> {
        let dirs = [
            &self.paths.config_dir,
            &self.paths.session_dir,
            &self.paths.preset_dir,
            &self.paths.log_dir,
        ];

        for dir in &dirs {
            if !dir.exists() {
                fs::create_dir_all(dir)?;
                info!("Created directory: {}", dir.display());
            }
        }

        Ok(())
    }

    /// Validate configuration values
    pub fn validate(&self) -> Result<()> {
        // Validate audio configuration
        if self.audio.sample_rate < 8000 || self.audio.sample_rate > 192000 {
            return Err(anyhow::anyhow!("Invalid sample rate: {}", self.audio.sample_rate));
        }

        if self.audio.buffer_size < 32 || self.audio.buffer_size > 8192 {
            return Err(anyhow::anyhow!("Invalid buffer size: {}", self.audio.buffer_size));
        }

        if self.audio.channels == 0 || self.audio.channels > 32 {
            return Err(anyhow::anyhow!("Invalid channel count: {}", self.audio.channels));
        }

        // Validate plugin configuration
        if self.plugins.max_plugins == 0 || self.plugins.max_plugins > 1000 {
            return Err(anyhow::anyhow!("Invalid max plugins: {}", self.plugins.max_plugins));
        }

        // Validate UI configuration
        if self.ui.window_width < 640 || self.ui.window_height < 480 {
            return Err(anyhow::anyhow!("Invalid window dimensions"));
        }

        Ok(())
    }

    /// Get configuration as JSON string (for debugging/export)
    pub fn to_json(&self) -> Result<String> {
        Ok(serde_json::to_string_pretty(self)?)
    }

    /// Update audio configuration
    pub fn update_audio(&mut self, audio_config: AudioConfiguration) -> Result<()> {
        // Validate before applying
        let temp_config = ApplicationConfig {
            audio: audio_config.clone(),
            ..self.clone()
        };
        temp_config.validate()?;
        
        self.audio = audio_config;
        debug!("Updated audio configuration");
        Ok(())
    }

    /// Update plugin configuration
    pub fn update_plugins(&mut self, plugin_config: PluginConfiguration) -> Result<()> {
        // Validate before applying
        let temp_config = ApplicationConfig {
            plugins: plugin_config.clone(),
            ..self.clone()
        };
        temp_config.validate()?;
        
        self.plugins = plugin_config;
        debug!("Updated plugin configuration");
        Ok(())
    }

    /// Update UI configuration
    pub fn update_ui(&mut self, ui_config: UIConfiguration) -> Result<()> {
        // Validate before applying
        let temp_config = ApplicationConfig {
            ui: ui_config.clone(),
            ..self.clone()
        };
        temp_config.validate()?;
        
        self.ui = ui_config;
        debug!("Updated UI configuration");
        Ok(())
    }

    /// Add plugin scan path
    pub fn add_plugin_path<P: AsRef<Path>>(&mut self, path: P) {
        let path_buf = path.as_ref().to_path_buf();
        if !self.plugins.scan_paths.contains(&path_buf) {
            self.plugins.scan_paths.push(path_buf);
            debug!("Added plugin scan path: {}", path.as_ref().display());
        }
    }

    /// Remove plugin scan path
    pub fn remove_plugin_path<P: AsRef<Path>>(&mut self, path: P) {
        let path_buf = path.as_ref().to_path_buf();
        self.plugins.scan_paths.retain(|p| p != &path_buf);
        debug!("Removed plugin scan path: {}", path.as_ref().display());
    }

    /// Blacklist a plugin
    pub fn blacklist_plugin(&mut self, plugin_name: &str) {
        if !self.plugins.blacklisted_plugins.contains(&plugin_name.to_string()) {
            self.plugins.blacklisted_plugins.push(plugin_name.to_string());
            debug!("Blacklisted plugin: {}", plugin_name);
        }
    }

    /// Remove plugin from blacklist
    pub fn unblacklist_plugin(&mut self, plugin_name: &str) {
        self.plugins.blacklisted_plugins.retain(|p| p != plugin_name);
        debug!("Removed plugin from blacklist: {}", plugin_name);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn test_default_config() {
        let config = ApplicationConfig::default();
        assert_eq!(config.audio.sample_rate, 44100);
        assert_eq!(config.audio.buffer_size, 512);
        assert_eq!(config.audio.channels, 2);
        assert_eq!(config.plugins.max_plugins, 64);
        assert!(config.plugins.enable_safety_checks);
    }

    #[test]
    fn test_config_validation() {
        let mut config = ApplicationConfig::default();
        
        // Valid config should pass
        assert!(config.validate().is_ok());
        
        // Invalid sample rate should fail
        config.audio.sample_rate = 1000;
        assert!(config.validate().is_err());
        
        // Reset and test invalid buffer size
        config = ApplicationConfig::default();
        config.audio.buffer_size = 16;
        assert!(config.validate().is_err());
    }

    #[test]
    fn test_config_serialization() {
        let config = ApplicationConfig::default();
        
        // Test TOML serialization
        let toml_str = toml::to_string(&config).unwrap();
        let deserialized: ApplicationConfig = toml::from_str(&toml_str).unwrap();
        assert_eq!(config.audio.sample_rate, deserialized.audio.sample_rate);
        
        // Test JSON export
        let json_str = config.to_json().unwrap();
        assert!(json_str.contains("sample_rate"));
    }

    #[test]
    fn test_config_save_load() {
        let temp_dir = TempDir::new().unwrap();
        let config_path = temp_dir.path().join("test_config.toml");
        
        let original_config = ApplicationConfig::default();
        original_config.save_to(&config_path).unwrap();
        
        let loaded_config = ApplicationConfig::load(&config_path).unwrap();
        assert_eq!(original_config.audio.sample_rate, loaded_config.audio.sample_rate);
        assert_eq!(original_config.plugins.max_plugins, loaded_config.plugins.max_plugins);
    }

    #[test]
    fn test_plugin_path_management() {
        let mut config = ApplicationConfig::default();
        let test_path = PathBuf::from("/test/path");
        
        // Add path
        config.add_plugin_path(&test_path);
        assert!(config.plugins.scan_paths.contains(&test_path));
        
        // Remove path
        config.remove_plugin_path(&test_path);
        assert!(!config.plugins.scan_paths.contains(&test_path));
    }

    #[test]
    fn test_plugin_blacklist() {
        let mut config = ApplicationConfig::default();
        let plugin_name = "BadPlugin";
        
        // Blacklist plugin
        config.blacklist_plugin(plugin_name);
        assert!(config.plugins.blacklisted_plugins.contains(&plugin_name.to_string()));
        
        // Unblacklist plugin
        config.unblacklist_plugin(plugin_name);
        assert!(!config.plugins.blacklisted_plugins.contains(&plugin_name.to_string()));
    }
}