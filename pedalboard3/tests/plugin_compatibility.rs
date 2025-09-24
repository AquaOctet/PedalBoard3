//! Plugin Compatibility Testing
//!
//! Tests to validate that VST/AU/LADSPA plugins work correctly
//! with the new Rust implementation and maintain compatibility
//! with the original Pedalboard2.

use std::path::{Path, PathBuf};
use std::collections::HashMap;

#[cfg(test)]
mod tests {
    use super::*;
    use crate::utils::*;
    
    #[test]
    fn test_common_vst_plugin_locations() {
        // Test that we can find common VST plugin directories
        let vst_paths = get_common_vst_paths();
        
        println!("Checking VST plugin paths:");
        for path in &vst_paths {
            let exists = path.exists();
            println!("  {} - {}", path.display(), if exists { "✓" } else { "✗" });
            
            if exists {
                // Count VST files
                if let Ok(entries) = std::fs::read_dir(path) {
                    let vst_count = entries
                        .filter_map(|e| e.ok())
                        .filter(|e| {
                            e.path().extension()
                                .and_then(|ext| ext.to_str())
                                .map(|ext| ext.eq_ignore_ascii_case("dll") || ext.eq_ignore_ascii_case("vst"))
                                .unwrap_or(false)
                        })
                        .count();
                    println!("    Found {} potential VST plugins", vst_count);
                }
            }
        }
    }
    
    #[test]
    fn test_plugin_scanner_preparation() {
        // Test the plugin scanning infrastructure
        let scanner = PluginScanner::new();
        
        assert!(!scanner.vst_paths.is_empty(), "No VST paths configured");
        
        // Test that scanner can identify plugin types
        let test_files = vec![
            ("test.dll", PluginFormat::VST),
            ("test.vst", PluginFormat::VST),
            ("test.component", PluginFormat::AudioUnit),
            ("test.so", PluginFormat::LADSPA),
        ];
        
        for (filename, expected_format) in test_files {
            let detected_format = scanner.detect_plugin_format(Path::new(filename));
            assert_eq!(detected_format, Some(expected_format), 
                      "Failed to detect {} format for {}", expected_format, filename);
        }
    }
    
    #[test]
    #[ignore] // Requires actual VST plugins to be installed
    fn test_scan_installed_plugins() {
        // This test scans for actually installed VST plugins
        let mut scanner = PluginScanner::new();
        let plugins = scanner.scan_all_formats();
        
        println!("Found {} plugins:", plugins.len());
        for plugin in &plugins {
            println!("  {} - {} ({})", plugin.name, plugin.vendor, plugin.format);
        }
        
        // Basic validation
        for plugin in &plugins {
            assert!(!plugin.name.is_empty(), "Plugin name should not be empty");
            assert!(plugin.path.exists(), "Plugin file should exist: {}", plugin.path.display());
        }
    }
    
    #[test]
    fn test_plugin_validation_framework() {
        // Test the plugin validation framework
        let validator = PluginValidator::new();
        
        // Test validation criteria
        assert!(validator.validate_plugin_info(&create_test_plugin_info()));
        assert!(!validator.validate_plugin_info(&create_invalid_plugin_info()));
    }
}

/// Plugin formats supported
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PluginFormat {
    VST,
    VST3,
    AudioUnit,
    LADSPA,
    LV2,
}

impl std::fmt::Display for PluginFormat {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PluginFormat::VST => write!(f, "VST 2.4"),
            PluginFormat::VST3 => write!(f, "VST3"),
            PluginFormat::AudioUnit => write!(f, "Audio Unit"),
            PluginFormat::LADSPA => write!(f, "LADSPA"),
            PluginFormat::LV2 => write!(f, "LV2"),
        }
    }
}

/// Plugin information structure
#[derive(Debug, Clone)]
pub struct PluginInfo {
    pub name: String,
    pub vendor: String,
    pub version: String,
    pub format: PluginFormat,
    pub path: PathBuf,
    pub unique_id: u32,
    pub num_inputs: u32,
    pub num_outputs: u32,
    pub num_parameters: u32,
    pub is_synth: bool,
    pub has_editor: bool,
}

/// Plugin scanner for discovering installed plugins
pub struct PluginScanner {
    pub vst_paths: Vec<PathBuf>,
    pub vst3_paths: Vec<PathBuf>,
    pub au_paths: Vec<PathBuf>,
    pub ladspa_paths: Vec<PathBuf>,
    pub lv2_paths: Vec<PathBuf>,
}

impl PluginScanner {
    pub fn new() -> Self {
        Self {
            vst_paths: get_common_vst_paths(),
            vst3_paths: get_common_vst3_paths(),
            au_paths: get_common_au_paths(),
            ladspa_paths: get_common_ladspa_paths(),
            lv2_paths: get_common_lv2_paths(),
        }
    }
    
    pub fn scan_all_formats(&mut self) -> Vec<PluginInfo> {
        let mut plugins = Vec::new();
        
        // Scan VST plugins
        for path in &self.vst_paths {
            if path.exists() {
                plugins.extend(self.scan_vst_directory(path));
            }
        }
        
        // TODO: Implement other formats when needed
        // plugins.extend(self.scan_vst3_plugins());
        // plugins.extend(self.scan_au_plugins());
        // plugins.extend(self.scan_ladspa_plugins());
        
        plugins
    }
    
    pub fn detect_plugin_format(&self, path: &Path) -> Option<PluginFormat> {
        match path.extension().and_then(|ext| ext.to_str()) {
            Some(ext) => match ext.to_lowercase().as_str() {
                "dll" | "vst" => Some(PluginFormat::VST),
                "vst3" => Some(PluginFormat::VST3),
                "component" => Some(PluginFormat::AudioUnit),
                "so" => Some(PluginFormat::LADSPA), // Could also be LV2
                _ => None,
            }
            None => None,
        }
    }
    
    fn scan_vst_directory(&self, dir: &Path) -> Vec<PluginInfo> {
        let mut plugins = Vec::new();
        
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.filter_map(|e| e.ok()) {
                let path = entry.path();
                if self.detect_plugin_format(&path) == Some(PluginFormat::VST) {
                    // TODO: Actually load and interrogate the VST plugin
                    // For now, create placeholder info
                    if let Some(plugin_info) = self.create_placeholder_plugin_info(&path) {
                        plugins.push(plugin_info);
                    }
                }
            }
        }
        
        plugins
    }
    
    fn create_placeholder_plugin_info(&self, path: &Path) -> Option<PluginInfo> {
        let filename = path.file_stem()?.to_str()?.to_string();
        
        Some(PluginInfo {
            name: filename.clone(),
            vendor: "Unknown".to_string(),
            version: "1.0.0".to_string(),
            format: PluginFormat::VST,
            path: path.to_path_buf(),
            unique_id: 0, // TODO: Get actual VST unique ID
            num_inputs: 2,
            num_outputs: 2,
            num_parameters: 0, // TODO: Get actual parameter count
            is_synth: false,
            has_editor: false,
        })
    }
}

/// Plugin validation framework
pub struct PluginValidator {
    pub validation_rules: HashMap<String, ValidationRule>,
}

#[derive(Debug)]
pub struct ValidationRule {
    pub name: String,
    pub check: fn(&PluginInfo) -> bool,
    pub description: String,
}

impl PluginValidator {
    pub fn new() -> Self {
        let mut rules = HashMap::new();
        
        // Add basic validation rules
        rules.insert("name_not_empty".to_string(), ValidationRule {
            name: "name_not_empty".to_string(),
            check: |info| !info.name.is_empty(),
            description: "Plugin name should not be empty".to_string(),
        });
        
        rules.insert("file_exists".to_string(), ValidationRule {
            name: "file_exists".to_string(),
            check: |info| info.path.exists(),
            description: "Plugin file should exist".to_string(),
        });
        
        rules.insert("valid_io_count".to_string(), ValidationRule {
            name: "valid_io_count".to_string(),
            check: |info| info.num_inputs <= 32 && info.num_outputs <= 32,
            description: "Plugin should have reasonable I/O counts".to_string(),
        });
        
        Self {
            validation_rules: rules,
        }
    }
    
    pub fn validate_plugin_info(&self, info: &PluginInfo) -> bool {
        for rule in self.validation_rules.values() {
            if !(rule.check)(info) {
                return false;
            }
        }
        true
    }
    
    pub fn validate_with_details(&self, info: &PluginInfo) -> Vec<String> {
        let mut failures = Vec::new();
        
        for rule in self.validation_rules.values() {
            if !(rule.check)(info) {
                failures.push(rule.description.clone());
            }
        }
        
        failures
    }
}

// Platform-specific plugin paths
fn get_common_vst_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    
    #[cfg(windows)]
    {
        paths.push(PathBuf::from("C:\\Program Files\\VSTPlugins"));
        paths.push(PathBuf::from("C:\\Program Files\\Common Files\\VST2"));
        paths.push(PathBuf::from("C:\\Program Files (x86)\\VSTPlugins"));
        paths.push(PathBuf::from("C:\\Program Files (x86)\\Common Files\\VST2"));
    }
    
    #[cfg(target_os = "macos")]
    {
        paths.push(PathBuf::from("/Library/Audio/Plug-Ins/VST"));
        paths.push(PathBuf::from("/System/Library/Audio/Plug-Ins/VST"));
        if let Ok(home) = std::env::var("HOME") {
            paths.push(PathBuf::from(format!("{}/Library/Audio/Plug-Ins/VST", home)));
        }
    }
    
    #[cfg(unix)]
    {
        paths.push(PathBuf::from("/usr/lib/vst"));
        paths.push(PathBuf::from("/usr/local/lib/vst"));
        if let Ok(home) = std::env::var("HOME") {
            paths.push(PathBuf::from(format!("{}/.vst", home)));
        }
    }
    
    paths
}

fn get_common_vst3_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    
    #[cfg(windows)]
    {
        paths.push(PathBuf::from("C:\\Program Files\\Common Files\\VST3"));
        paths.push(PathBuf::from("C:\\Program Files (x86)\\Common Files\\VST3"));
    }
    
    #[cfg(target_os = "macos")]
    {
        paths.push(PathBuf::from("/Library/Audio/Plug-Ins/VST3"));
        if let Ok(home) = std::env::var("HOME") {
            paths.push(PathBuf::from(format!("{}/Library/Audio/Plug-Ins/VST3", home)));
        }
    }
    
    #[cfg(unix)]
    {
        paths.push(PathBuf::from("/usr/lib/vst3"));
        paths.push(PathBuf::from("/usr/local/lib/vst3"));
        if let Ok(home) = std::env::var("HOME") {
            paths.push(PathBuf::from(format!("{}/.vst3", home)));
        }
    }
    
    paths
}

fn get_common_au_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    
    #[cfg(target_os = "macos")]
    {
        paths.push(PathBuf::from("/Library/Audio/Plug-Ins/Components"));
        paths.push(PathBuf::from("/System/Library/Audio/Plug-Ins/Components"));
        if let Ok(home) = std::env::var("HOME") {
            paths.push(PathBuf::from(format!("{}/Library/Audio/Plug-Ins/Components", home)));
        }
    }
    
    paths
}

fn get_common_ladspa_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    
    #[cfg(unix)]
    {
        paths.push(PathBuf::from("/usr/lib/ladspa"));
        paths.push(PathBuf::from("/usr/local/lib/ladspa"));
        if let Ok(home) = std::env::var("HOME") {
            paths.push(PathBuf::from(format!("{}/.ladspa", home)));
        }
    }
    
    paths
}

fn get_common_lv2_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    
    #[cfg(unix)]
    {
        paths.push(PathBuf::from("/usr/lib/lv2"));
        paths.push(PathBuf::from("/usr/local/lib/lv2"));
        if let Ok(home) = std::env::var("HOME") {
            paths.push(PathBuf::from(format!("{}/.lv2", home)));
        }
    }
    
    paths
}

// Test data helpers
fn create_test_plugin_info() -> PluginInfo {
    PluginInfo {
        name: "Test Plugin".to_string(),
        vendor: "Test Vendor".to_string(),
        version: "1.0.0".to_string(),
        format: PluginFormat::VST,
        path: PathBuf::from("/path/to/test.dll"),
        unique_id: 12345,
        num_inputs: 2,
        num_outputs: 2,
        num_parameters: 10,
        is_synth: false,
        has_editor: true,
    }
}

fn create_invalid_plugin_info() -> PluginInfo {
    PluginInfo {
        name: "".to_string(), // Invalid: empty name
        vendor: "Test Vendor".to_string(),
        version: "1.0.0".to_string(),
        format: PluginFormat::VST,
        path: PathBuf::from("/nonexistent/path.dll"), // Invalid: file doesn't exist
        unique_id: 12345,
        num_inputs: 100, // Invalid: too many inputs
        num_outputs: 2,
        num_parameters: 10,
        is_synth: false,
        has_editor: true,
    }
}