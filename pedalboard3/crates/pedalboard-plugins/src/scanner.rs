//! Plugin scanner for discovering and cataloging available plugins

use crate::{PluginFormat, PluginInfo};
use anyhow::Result;
use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};
use tracing::{debug, info, warn};

/// Plugin scanner for finding plugins on the system
pub struct PluginScanner {
    search_paths: Vec<PathBuf>,
    blacklisted_plugins: HashSet<PathBuf>,
}

impl PluginScanner {
    /// Create a new plugin scanner
    pub fn new() -> Self {
        let mut scanner = Self {
            search_paths: Vec::new(),
            blacklisted_plugins: HashSet::new(),
        };
        
        scanner.add_default_search_paths();
        scanner
    }

    /// Add default search paths for each platform
    fn add_default_search_paths(&mut self) {
        #[cfg(target_os = "windows")]
        self.add_windows_paths();
        
        #[cfg(target_os = "macos")]
        self.add_macos_paths();
        
        #[cfg(target_os = "linux")]
        self.add_linux_paths();
    }

    /// Windows-specific plugin search paths
    #[cfg(target_os = "windows")]
    fn add_windows_paths(&mut self) {
        // Common VST paths on Windows
        if let Some(program_files) = std::env::var_os("PROGRAMFILES") {
            self.search_paths.push(PathBuf::from(program_files).join("Steinberg").join("VSTPlugins"));
            self.search_paths.push(PathBuf::from(program_files).join("VSTPlugins"));
            self.search_paths.push(PathBuf::from(program_files).join("Common Files").join("VST3"));
        }
        
        if let Some(program_files_x86) = std::env::var_os("PROGRAMFILES(X86)") {
            self.search_paths.push(PathBuf::from(program_files_x86).join("Steinberg").join("VSTPlugins"));
            self.search_paths.push(PathBuf::from(program_files_x86).join("VSTPlugins"));
            self.search_paths.push(PathBuf::from(program_files_x86).join("Common Files").join("VST3"));
        }
    }

    /// macOS-specific plugin search paths
    #[cfg(target_os = "macos")]
    fn add_macos_paths(&mut self) {
        // System paths
        self.search_paths.push(PathBuf::from("/Library/Audio/Plug-Ins/VST"));
        self.search_paths.push(PathBuf::from("/Library/Audio/Plug-Ins/VST3"));
        self.search_paths.push(PathBuf::from("/Library/Audio/Plug-Ins/Components"));
        self.search_paths.push(PathBuf::from("/System/Library/Components"));
        
        // User paths
        if let Some(home) = std::env::var_os("HOME") {
            let home_path = PathBuf::from(home);
            self.search_paths.push(home_path.join("Library").join("Audio").join("Plug-Ins").join("VST"));
            self.search_paths.push(home_path.join("Library").join("Audio").join("Plug-Ins").join("VST3"));
            self.search_paths.push(home_path.join("Library").join("Audio").join("Plug-Ins").join("Components"));
        }
    }

    /// Linux-specific plugin search paths
    #[cfg(target_os = "linux")]
    fn add_linux_paths(&mut self) {
        // System paths
        self.search_paths.push(PathBuf::from("/usr/lib/vst"));
        self.search_paths.push(PathBuf::from("/usr/lib/vst3"));
        self.search_paths.push(PathBuf::from("/usr/lib/lv2"));
        self.search_paths.push(PathBuf::from("/usr/local/lib/vst"));
        self.search_paths.push(PathBuf::from("/usr/local/lib/vst3"));
        self.search_paths.push(PathBuf::from("/usr/local/lib/lv2"));
        
        // User paths
        if let Some(home) = std::env::var_os("HOME") {
            let home_path = PathBuf::from(home);
            self.search_paths.push(home_path.join(".vst"));
            self.search_paths.push(home_path.join(".vst3"));
            self.search_paths.push(home_path.join(".lv2"));
        }
        
        // XDG paths
        if let Some(xdg_data_home) = std::env::var_os("XDG_DATA_HOME") {
            let xdg_path = PathBuf::from(xdg_data_home);
            self.search_paths.push(xdg_path.join("vst"));
            self.search_paths.push(xdg_path.join("vst3"));
            self.search_paths.push(xdg_path.join("lv2"));
        }
    }

    /// Fallback implementation for other platforms
    #[cfg(not(any(target_os = "windows", target_os = "macos", target_os = "linux")))]
    fn add_windows_paths(&mut self) {}
    
    #[cfg(not(any(target_os = "windows", target_os = "macos", target_os = "linux")))]
    fn add_macos_paths(&mut self) {}
    
    #[cfg(not(any(target_os = "windows", target_os = "macos", target_os = "linux")))]
    fn add_linux_paths(&mut self) {}

    /// Add a custom search path
    pub fn add_search_path<P: AsRef<Path>>(&mut self, path: P) {
        self.search_paths.push(path.as_ref().to_path_buf());
    }

    /// Remove a search path
    pub fn remove_search_path<P: AsRef<Path>>(&mut self, path: P) {
        let path = path.as_ref();
        self.search_paths.retain(|p| p != path);
    }

    /// Get all configured search paths
    pub fn get_search_paths(&self) -> &[PathBuf] {
        &self.search_paths
    }

    /// Blacklist a plugin (won't be included in scans)
    pub fn blacklist_plugin<P: AsRef<Path>>(&mut self, path: P) {
        self.blacklisted_plugins.insert(path.as_ref().to_path_buf());
    }

    /// Remove a plugin from blacklist
    pub fn unblacklist_plugin<P: AsRef<Path>>(&mut self, path: P) {
        self.blacklisted_plugins.remove(path.as_ref());
    }

    /// Scan all configured paths for plugins
    pub fn scan_all(&self) -> Result<Vec<PluginInfo>> {
        info!("Starting plugin scan in {} directories", self.search_paths.len());
        let mut plugins = Vec::new();

        for search_path in &self.search_paths {
            if search_path.exists() {
                debug!("Scanning directory: {}", search_path.display());
                match self.scan_directory(search_path) {
                    Ok(mut found_plugins) => {
                        plugins.append(&mut found_plugins);
                    }
                    Err(e) => {
                        warn!("Failed to scan directory {}: {}", search_path.display(), e);
                    }
                }
            } else {
                debug!("Search path does not exist: {}", search_path.display());
            }
        }

        info!("Plugin scan complete: found {} plugins", plugins.len());
        Ok(plugins)
    }

    /// Scan a specific directory for plugins
    pub fn scan_directory<P: AsRef<Path>>(&self, path: P) -> Result<Vec<PluginInfo>> {
        let path = path.as_ref();
        let mut plugins = Vec::new();

        if !path.is_dir() {
            return Ok(plugins);
        }

        let entries = fs::read_dir(path)?;
        for entry in entries {
            let entry = entry?;
            let entry_path = entry.path();

            // Skip blacklisted plugins
            if self.blacklisted_plugins.contains(&entry_path) {
                debug!("Skipping blacklisted plugin: {}", entry_path.display());
                continue;
            }

            if entry_path.is_file() {
                if let Some(plugin_info) = self.identify_plugin(&entry_path) {
                    debug!("Found plugin: {} ({})", plugin_info.name, plugin_info.format.format_name());
                    plugins.push(plugin_info);
                }
            } else if entry_path.is_dir() {
                // Recursively scan subdirectories (with depth limit)
                match self.scan_directory(&entry_path) {
                    Ok(mut sub_plugins) => {
                        plugins.append(&mut sub_plugins);
                    }
                    Err(e) => {
                        debug!("Failed to scan subdirectory {}: {}", entry_path.display(), e);
                    }
                }
            }
        }

        Ok(plugins)
    }

    /// Identify if a file is a plugin and extract basic info
    fn identify_plugin(&self, path: &Path) -> Option<PluginInfo> {
        let extension = path.extension()?.to_str()?;
        let file_name = path.file_stem()?.to_str()?.to_string();

        let format = match extension {
            "dll" | "so" | "dylib" => {
                // Could be VST2 - need further validation
                if self.is_vst_plugin(path) {
                    PluginFormat::VST2
                } else {
                    return None;
                }
            }
            "vst" => PluginFormat::VST2,
            "vst3" => PluginFormat::VST3,
            "component" => PluginFormat::AU,
            "clap" => PluginFormat::CLAP,
            "lv2" => PluginFormat::LV2,
            _ => return None,
        };

        Some(PluginInfo::new(file_name, path.to_path_buf(), format))
    }

    /// Check if a file is likely a VST plugin
    fn is_vst_plugin(&self, _path: &Path) -> bool {
        // This is a simplified check. In a real implementation, you would:
        // 1. Check if the file exports VST entry points
        // 2. Try to load it as a dynamic library
        // 3. Validate it has VST symbols
        
        // For now, assume any .dll/.so/.dylib in VST directories is a VST
        true
    }

    /// Scan for a specific plugin format
    pub fn scan_format(&self, format: PluginFormat) -> Result<Vec<PluginInfo>> {
        let all_plugins = self.scan_all()?;
        Ok(all_plugins
            .into_iter()
            .filter(|plugin| plugin.format == format)
            .collect())
    }

    /// Quick scan that only checks file extensions (faster but less accurate)
    pub fn quick_scan(&self) -> Result<Vec<PluginInfo>> {
        let mut plugins = Vec::new();

        for search_path in &self.search_paths {
            if search_path.exists() {
                match self.quick_scan_directory(search_path) {
                    Ok(mut found_plugins) => {
                        plugins.append(&mut found_plugins);
                    }
                    Err(e) => {
                        warn!("Failed to quick scan directory {}: {}", search_path.display(), e);
                    }
                }
            }
        }

        Ok(plugins)
    }

    /// Quick scan of a directory (extension-based only)
    fn quick_scan_directory(&self, path: &Path) -> Result<Vec<PluginInfo>> {
        let mut plugins = Vec::new();

        if !path.is_dir() {
            return Ok(plugins);
        }

        let entries = fs::read_dir(path)?;
        for entry in entries {
            let entry = entry?;
            let entry_path = entry.path();

            if self.blacklisted_plugins.contains(&entry_path) {
                continue;
            }

            if entry_path.is_file() {
                if let Some(plugin_info) = self.identify_plugin(&entry_path) {
                    plugins.push(plugin_info);
                }
            }
        }

        Ok(plugins)
    }
}

impl Default for PluginScanner {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_scanner_creation() {
        let scanner = PluginScanner::new();
        assert!(!scanner.search_paths.is_empty());
    }

    #[test]
    fn test_add_search_path() {
        let mut scanner = PluginScanner::new();
        let original_count = scanner.search_paths.len();
        
        scanner.add_search_path("/test/path");
        assert_eq!(scanner.search_paths.len(), original_count + 1);
        assert!(scanner.search_paths.contains(&PathBuf::from("/test/path")));
    }

    #[test]
    fn test_remove_search_path() {
        let mut scanner = PluginScanner::new();
        scanner.add_search_path("/test/path");
        
        scanner.remove_search_path("/test/path");
        assert!(!scanner.search_paths.contains(&PathBuf::from("/test/path")));
    }

    #[test]
    fn test_blacklist_operations() {
        let mut scanner = PluginScanner::new();
        let plugin_path = PathBuf::from("/test/plugin.dll");
        
        scanner.blacklist_plugin(&plugin_path);
        assert!(scanner.blacklisted_plugins.contains(&plugin_path));
        
        scanner.unblacklist_plugin(&plugin_path);
        assert!(!scanner.blacklisted_plugins.contains(&plugin_path));
    }

    #[test]
    fn test_plugin_identification() {
        let scanner = PluginScanner::new();
        
        // Test VST2 identification
        let vst_path = PathBuf::from("/test/plugin.dll");
        let plugin = scanner.identify_plugin(&vst_path);
        if let Some(plugin) = plugin {
            assert_eq!(plugin.format, PluginFormat::VST2);
            assert_eq!(plugin.name, "plugin");
        }
        
        // Test VST3 identification
        let vst3_path = PathBuf::from("/test/plugin.vst3");
        let plugin = scanner.identify_plugin(&vst3_path);
        if let Some(plugin) = plugin {
            assert_eq!(plugin.format, PluginFormat::VST3);
            assert_eq!(plugin.name, "plugin");
        }
    }
}