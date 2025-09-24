//! Session management for saving and loading complete pedalboard configurations

use anyhow::Result;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::Path;
use uuid::Uuid;

use crate::{ApplicationConfig, AudioConfiguration};

/// Complete session data
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Session {
    pub name: String,
    pub version: String,
    pub created: chrono::DateTime<chrono::Utc>,
    pub modified: chrono::DateTime<chrono::Utc>,
    pub audio_config: AudioConfiguration,
    pub plugin_chain: Vec<SessionPlugin>,
    pub metadata: HashMap<String, String>,
}

/// Plugin instance in a session
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SessionPlugin {
    pub id: Uuid,
    pub plugin_id: Uuid,
    pub name: String,
    pub bypass: bool,
    pub parameters: HashMap<u32, f32>,
    pub input_gain: f32,
    pub output_gain: f32,
    pub position: usize,
}

impl Session {
    /// Create a new empty session
    pub fn new(name: String) -> Self {
        let now = chrono::Utc::now();
        
        Self {
            name,
            version: "3.0.0".to_string(),
            created: now,
            modified: now,
            audio_config: AudioConfiguration::default(),
            plugin_chain: Vec::new(),
            metadata: HashMap::new(),
        }
    }

    /// Create session from current application state
    pub fn create_from_current_state(app_config: &ApplicationConfig) -> Self {
        let mut session = Self::new("Current Session".to_string());
        session.audio_config = app_config.audio.clone();
        
        // Add metadata
        session.metadata.insert("created_by".to_string(), "Pedalboard3".to_string());
        session.metadata.insert("platform".to_string(), std::env::consts::OS.to_string());
        
        session
    }

    /// Save session to file
    pub fn save<P: AsRef<Path>>(&self, path: P) -> Result<()> {
        let content = serde_json::to_string_pretty(self)?;
        fs::write(path.as_ref(), content)?;
        Ok(())
    }

    /// Load session from file
    pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
        let content = fs::read_to_string(path.as_ref())?;
        let mut session: Session = serde_json::from_str(&content)?;
        session.modified = chrono::Utc::now();
        Ok(session)
    }

    /// Add plugin to session
    pub fn add_plugin(&mut self, plugin: SessionPlugin) {
        self.plugin_chain.push(plugin);
        self.modified = chrono::Utc::now();
    }

    /// Remove plugin from session
    pub fn remove_plugin(&mut self, plugin_id: Uuid) {
        self.plugin_chain.retain(|p| p.id != plugin_id);
        self.modified = chrono::Utc::now();
    }

    /// Update plugin parameters
    pub fn update_plugin_parameters(&mut self, plugin_id: Uuid, parameters: HashMap<u32, f32>) {
        if let Some(plugin) = self.plugin_chain.iter_mut().find(|p| p.id == plugin_id) {
            plugin.parameters = parameters;
            self.modified = chrono::Utc::now();
        }
    }

    /// Set plugin bypass state
    pub fn set_plugin_bypass(&mut self, plugin_id: Uuid, bypass: bool) {
        if let Some(plugin) = self.plugin_chain.iter_mut().find(|p| p.id == plugin_id) {
            plugin.bypass = bypass;
            self.modified = chrono::Utc::now();
        }
    }

    /// Reorder plugins
    pub fn reorder_plugins(&mut self, new_order: Vec<Uuid>) -> Result<()> {
        let mut new_chain = Vec::new();
        
        for (position, &plugin_id) in new_order.iter().enumerate() {
            if let Some(mut plugin) = self.plugin_chain.iter().find(|p| p.id == plugin_id).cloned() {
                plugin.position = position;
                new_chain.push(plugin);
            }
        }
        
        if new_chain.len() != self.plugin_chain.len() {
            return Err(anyhow::anyhow!("Plugin reorder failed: count mismatch"));
        }
        
        self.plugin_chain = new_chain;
        self.modified = chrono::Utc::now();
        Ok(())
    }

    /// Get plugins in processing order
    pub fn get_ordered_plugins(&self) -> Vec<&SessionPlugin> {
        let mut plugins: Vec<&SessionPlugin> = self.plugin_chain.iter().collect();
        plugins.sort_by_key(|p| p.position);
        plugins
    }

    /// Add metadata
    pub fn add_metadata(&mut self, key: String, value: String) {
        self.metadata.insert(key, value);
        self.modified = chrono::Utc::now();
    }

    /// Get metadata
    pub fn get_metadata(&self, key: &str) -> Option<&String> {
        self.metadata.get(key)
    }

    /// Clone session with new name
    pub fn clone_with_name(&self, new_name: String) -> Self {
        let mut cloned = self.clone();
        cloned.name = new_name;
        cloned.created = chrono::Utc::now();
        cloned.modified = chrono::Utc::now();
        cloned
    }
}

impl SessionPlugin {
    /// Create a new session plugin
    pub fn new(plugin_id: Uuid, name: String) -> Self {
        Self {
            id: Uuid::new_v4(),
            plugin_id,
            name,
            bypass: false,
            parameters: HashMap::new(),
            input_gain: 1.0,
            output_gain: 1.0,
            position: 0,
        }
    }

    /// Set parameter value
    pub fn set_parameter(&mut self, index: u32, value: f32) {
        self.parameters.insert(index, value.clamp(0.0, 1.0));
    }

    /// Get parameter value
    pub fn get_parameter(&self, index: u32) -> Option<f32> {
        self.parameters.get(&index).copied()
    }

    /// Get all parameters as sorted vector
    pub fn get_parameters_sorted(&self) -> Vec<(u32, f32)> {
        let mut params: Vec<_> = self.parameters.iter().map(|(&k, &v)| (k, v)).collect();
        params.sort_by_key(|&(k, _)| k);
        params
    }
}

/// Session manager for handling multiple sessions
pub struct SessionManager {
    current_session: Option<Session>,
    session_directory: std::path::PathBuf,
}

impl SessionManager {
    /// Create a new session manager
    pub fn new(session_directory: std::path::PathBuf) -> Result<Self> {
        // Ensure session directory exists
        if !session_directory.exists() {
            fs::create_dir_all(&session_directory)?;
        }

        Ok(Self {
            current_session: None,
            session_directory,
        })
    }

    /// Create a new session
    pub fn new_session(&mut self, name: String) -> Result<()> {
        let session = Session::new(name);
        self.current_session = Some(session);
        Ok(())
    }

    /// Load session from file
    pub fn load_session<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
        let session = Session::load(path)?;
        self.current_session = Some(session);
        Ok(())
    }

    /// Save current session
    pub fn save_current_session(&self) -> Result<()> {
        if let Some(ref session) = self.current_session {
            let filename = format!("{}.pbsession", session.name.replace(' ', "_"));
            let path = self.session_directory.join(filename);
            session.save(path)?;
        } else {
            return Err(anyhow::anyhow!("No current session to save"));
        }
        Ok(())
    }

    /// Save session to specific path
    pub fn save_session_as<P: AsRef<Path>>(&self, path: P) -> Result<()> {
        if let Some(ref session) = self.current_session {
            session.save(path)?;
        } else {
            return Err(anyhow::anyhow!("No current session to save"));
        }
        Ok(())
    }

    /// Get current session
    pub fn get_current_session(&self) -> Option<&Session> {
        self.current_session.as_ref()
    }

    /// Get mutable current session
    pub fn get_current_session_mut(&mut self) -> Option<&mut Session> {
        self.current_session.as_mut()
    }

    /// List available session files
    pub fn list_sessions(&self) -> Result<Vec<std::path::PathBuf>> {
        let mut sessions = Vec::new();

        if self.session_directory.exists() {
            for entry in fs::read_dir(&self.session_directory)? {
                let entry = entry?;
                let path = entry.path();
                
                if path.is_file() && path.extension().and_then(|s| s.to_str()) == Some("pbsession") {
                    sessions.push(path);
                }
            }
        }

        sessions.sort();
        Ok(sessions)
    }

    /// Delete a session file
    pub fn delete_session<P: AsRef<Path>>(&self, path: P) -> Result<()> {
        fs::remove_file(path.as_ref())?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn test_session_creation() {
        let session = Session::new("Test Session".to_string());
        assert_eq!(session.name, "Test Session");
        assert_eq!(session.version, "3.0.0");
        assert!(session.plugin_chain.is_empty());
    }

    #[test]
    fn test_session_plugin() {
        let plugin_id = Uuid::new_v4();
        let mut plugin = SessionPlugin::new(plugin_id, "Test Plugin".to_string());
        
        plugin.set_parameter(0, 0.5);
        assert_eq!(plugin.get_parameter(0), Some(0.5));
        
        plugin.set_parameter(1, 1.5); // Should be clamped to 1.0
        assert_eq!(plugin.get_parameter(1), Some(1.0));
    }

    #[test]
    fn test_session_save_load() {
        let temp_dir = TempDir::new().unwrap();
        let session_path = temp_dir.path().join("test.pbsession");
        
        // Create and save session
        let mut original_session = Session::new("Test".to_string());
        let plugin = SessionPlugin::new(Uuid::new_v4(), "Plugin".to_string());
        original_session.add_plugin(plugin);
        
        original_session.save(&session_path).unwrap();
        
        // Load session
        let loaded_session = Session::load(&session_path).unwrap();
        assert_eq!(loaded_session.name, "Test");
        assert_eq!(loaded_session.plugin_chain.len(), 1);
    }

    #[test]
    fn test_session_manager() {
        let temp_dir = TempDir::new().unwrap();
        let mut manager = SessionManager::new(temp_dir.path().to_path_buf()).unwrap();
        
        // Create new session
        manager.new_session("Test Session".to_string()).unwrap();
        assert!(manager.get_current_session().is_some());
        assert_eq!(manager.get_current_session().unwrap().name, "Test Session");
        
        // Save session
        manager.save_current_session().unwrap();
        
        // List sessions
        let sessions = manager.list_sessions().unwrap();
        assert_eq!(sessions.len(), 1);
    }

    #[test]
    fn test_plugin_ordering() {
        let mut session = Session::new("Test".to_string());
        
        let plugin1 = SessionPlugin::new(Uuid::new_v4(), "Plugin 1".to_string());
        let plugin2 = SessionPlugin::new(Uuid::new_v4(), "Plugin 2".to_string());
        let plugin3 = SessionPlugin::new(Uuid::new_v4(), "Plugin 3".to_string());
        
        let id1 = plugin1.id;
        let id2 = plugin2.id;
        let id3 = plugin3.id;
        
        session.add_plugin(plugin1);
        session.add_plugin(plugin2);
        session.add_plugin(plugin3);
        
        // Reorder plugins
        let new_order = vec![id3, id1, id2];
        session.reorder_plugins(new_order).unwrap();
        
        let ordered = session.get_ordered_plugins();
        assert_eq!(ordered[0].id, id3);
        assert_eq!(ordered[1].id, id1);
        assert_eq!(ordered[2].id, id2);
    }
}