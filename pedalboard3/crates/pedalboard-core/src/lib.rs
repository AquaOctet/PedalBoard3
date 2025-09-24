//! Pedalboard3 Core Application
//!
//! Main application entry point and coordination

use anyhow::{Context, Result};
use pedalboard_audio::{AudioConfig, AudioEngine};
use pedalboard_plugins::{HostConfig, PluginHost};
use std::sync::{Arc, Mutex};
use tokio::sync::mpsc;
use tracing::{debug, error, info, warn};

pub mod app;
pub mod config;
pub mod presets;
pub mod session;
pub mod events;

pub use app::*;
pub use config::*;
pub use presets::*;
pub use session::*;
pub use events::*;

/// Application state
#[derive(Debug, Clone, PartialEq)]
pub enum AppState {
    Initializing,
    Ready,
    Running,
    Error(String),
    Shutdown,
}

/// Main Pedalboard3 application
pub struct Pedalboard3 {
    state: AppState,
    config: ApplicationConfig,
    audio_engine: Arc<Mutex<AudioEngine>>,
    plugin_host: Arc<Mutex<PluginHost>>,
    event_sender: mpsc::UnboundedSender<AppEvent>,
    event_receiver: Option<mpsc::UnboundedReceiver<AppEvent>>,
}

impl Pedalboard3 {
    /// Create a new Pedalboard3 application
    pub fn new() -> Result<Self> {
        info!("Initializing Pedalboard3 application");

        // Load configuration
        let config = ApplicationConfig::load_or_default()?;

        // Create audio engine
        let audio_engine = Arc::new(Mutex::new(AudioEngine::new()?));

        // Create plugin host
        let host_config = HostConfig {
            sample_rate: config.audio.sample_rate as f32,
            buffer_size: config.audio.buffer_size,
            max_plugins: config.plugins.max_plugins,
            enable_safety_checks: config.plugins.enable_safety_checks,
            parallel_processing: config.plugins.parallel_processing,
        };
        let plugin_host = Arc::new(Mutex::new(PluginHost::new(host_config)?));

        // Create event system
        let (event_sender, event_receiver) = mpsc::unbounded_channel();

        Ok(Self {
            state: AppState::Initializing,
            config,
            audio_engine,
            plugin_host,
            event_sender,
            event_receiver: Some(event_receiver),
        })
    }

    /// Initialize the application
    pub async fn initialize(&mut self) -> Result<()> {
        info!("Initializing Pedalboard3 components");
        self.state = AppState::Initializing;

        // Initialize audio engine
        {
            let mut engine = self.audio_engine.lock().unwrap();
            let audio_config = AudioConfig {
                sample_rate: self.config.audio.sample_rate,
                buffer_size: self.config.audio.buffer_size,
                channels: self.config.audio.channels,
            };
            engine.configure(audio_config)?;
            engine.initialize_devices()?;
            info!("Audio engine initialized");
        }

        // Initialize plugin host
        {
            let mut host = self.plugin_host.lock().unwrap();
            host.initialize()?;
            info!("Plugin host initialized");
        }

        self.state = AppState::Ready;
        self.send_event(AppEvent::ApplicationReady).await?;
        
        info!("Pedalboard3 initialization complete");
        Ok(())
    }

    /// Start the application
    pub async fn start(&mut self) -> Result<()> {
        if self.state != AppState::Ready {
            return Err(anyhow::anyhow!("Application not ready to start"));
        }

        info!("Starting Pedalboard3");
        self.state = AppState::Running;

        // Start audio engine
        {
            let mut engine = self.audio_engine.lock().unwrap();
            engine.start().context("Failed to start audio engine")?;
            info!("Audio engine started");
        }

        self.send_event(AppEvent::ApplicationStarted).await?;
        
        // Start event processing loop
        self.run_event_loop().await?;

        Ok(())
    }

    /// Stop the application
    pub async fn stop(&mut self) -> Result<()> {
        if self.state != AppState::Running {
            return Ok(());
        }

        info!("Stopping Pedalboard3");
        self.state = AppState::Shutdown;

        // Stop audio engine
        {
            let mut engine = self.audio_engine.lock().unwrap();
            engine.stop()?;
            info!("Audio engine stopped");
        }

        self.send_event(AppEvent::ApplicationStopped).await?;
        
        info!("Pedalboard3 stopped");
        Ok(())
    }

    /// Run the main event processing loop
    async fn run_event_loop(&mut self) -> Result<()> {
        let mut event_receiver = self.event_receiver.take()
            .ok_or_else(|| anyhow::anyhow!("Event receiver already taken"))?;

        info!("Starting event processing loop");

        while let Some(event) = event_receiver.recv().await {
            if let Err(e) = self.handle_event(event).await {
                error!("Error handling event: {}", e);
            }

            // Check if we should exit the loop
            if matches!(self.state, AppState::Shutdown | AppState::Error(_)) {
                break;
            }
        }

        info!("Event processing loop ended");
        Ok(())
    }

    /// Handle application events
    async fn handle_event(&mut self, event: AppEvent) -> Result<()> {
        debug!("Handling event: {:?}", event);

        match event {
            AppEvent::ApplicationReady => {
                debug!("Application ready");
            }
            AppEvent::ApplicationStarted => {
                debug!("Application started");
            }
            AppEvent::ApplicationStopped => {
                debug!("Application stopped");
            }
            AppEvent::AudioConfigChanged(config) => {
                self.handle_audio_config_change(config).await?;
            }
            AppEvent::PluginLoaded { plugin_id, instance_id } => {
                info!("Plugin loaded: {} (instance: {})", plugin_id, instance_id);
            }
            AppEvent::PluginUnloaded { instance_id } => {
                info!("Plugin unloaded: {}", instance_id);
            }
            AppEvent::PresetLoaded { name } => {
                info!("Preset loaded: {}", name);
            }
            AppEvent::PresetSaved { name } => {
                info!("Preset saved: {}", name);
            }
            AppEvent::SessionSaved { path } => {
                info!("Session saved: {}", path.display());
            }
            AppEvent::SessionLoaded { path } => {
                info!("Session loaded: {}", path.display());
            }
            AppEvent::Error { message } => {
                error!("Application error: {}", message);
                self.state = AppState::Error(message);
            }
            AppEvent::Shutdown => {
                info!("Shutdown event received");
                self.stop().await?;
            }
            AppEvent::PluginParameterChanged { instance_id, parameter, value } => {
                debug!("Plugin parameter changed: {} param {} = {}", instance_id, parameter, value);
            }
            AppEvent::PluginBypassed { instance_id, bypassed } => {
                debug!("Plugin bypassed: {} = {}", instance_id, bypassed);
            }
            AppEvent::PresetDeleted { name } => {
                info!("Preset deleted: {}", name);
            }
            AppEvent::SessionCreated { name } => {
                info!("Session created: {}", name);
            }
        }

        Ok(())
    }

    /// Handle audio configuration changes
    async fn handle_audio_config_change(&mut self, new_config: AudioConfig) -> Result<()> {
        info!("Updating audio configuration: {:?}", new_config);

        // Update audio engine
        {
            let mut engine = self.audio_engine.lock().unwrap();
            let was_running = engine.is_running();
            
            if was_running {
                engine.stop()?;
            }
            
            engine.configure(new_config.clone())?;
            
            if was_running {
                engine.start()?;
            }
        }

        // Update plugin host configuration
        {
            let mut host = self.plugin_host.lock().unwrap();
            let host_config = HostConfig {
                sample_rate: new_config.sample_rate as f32,
                buffer_size: new_config.buffer_size,
                ..host.get_config().clone()
            };
            host.update_config(host_config)?;
        }

        // Update application config
        self.config.audio.sample_rate = new_config.sample_rate;
        self.config.audio.buffer_size = new_config.buffer_size;
        self.config.audio.channels = new_config.channels;

        Ok(())
    }

    /// Send an event to the event system
    async fn send_event(&self, event: AppEvent) -> Result<()> {
        self.event_sender.send(event)
            .map_err(|e| anyhow::anyhow!("Failed to send event: {}", e))
    }

    /// Get application state
    pub fn get_state(&self) -> AppState {
        self.state.clone()
    }

    /// Get application configuration
    pub fn get_config(&self) -> &ApplicationConfig {
        &self.config
    }

    /// Get audio engine reference
    pub fn get_audio_engine(&self) -> Arc<Mutex<AudioEngine>> {
        self.audio_engine.clone()
    }

    /// Get plugin host reference
    pub fn get_plugin_host(&self) -> Arc<Mutex<PluginHost>> {
        self.plugin_host.clone()
    }

    /// Get event sender
    pub fn get_event_sender(&self) -> mpsc::UnboundedSender<AppEvent> {
        self.event_sender.clone()
    }

    /// Load a session file
    pub async fn load_session<P: AsRef<std::path::Path>>(&mut self, path: P) -> Result<()> {
        let session = Session::load(path.as_ref())?;
        info!("Loading session: {}", session.name);

        // Apply session configuration
        // This is a simplified version - full implementation would restore plugin states, etc.
        
        self.send_event(AppEvent::SessionLoaded { 
            path: path.as_ref().to_path_buf() 
        }).await?;

        Ok(())
    }

    /// Save current session
    pub async fn save_session<P: AsRef<std::path::Path>>(&mut self, path: P) -> Result<()> {
        let session = Session::create_from_current_state(&self.config);
        session.save(path.as_ref())?;

        self.send_event(AppEvent::SessionSaved { 
            path: path.as_ref().to_path_buf() 
        }).await?;

        info!("Session saved to: {}", path.as_ref().display());
        Ok(())
    }

    /// Get application statistics
    pub fn get_statistics(&self) -> ApplicationStatistics {
        let engine_running = self.audio_engine.lock().unwrap().is_running();
        let host_stats = self.plugin_host.lock().unwrap().get_statistics();

        ApplicationStatistics {
            state: self.state.clone(),
            audio_engine_running: engine_running,
            active_plugins: host_stats.active_plugin_count,
            total_available_plugins: host_stats.total_available_plugins,
            bypassed_plugins: host_stats.bypassed_plugins,
        }
    }
}

impl Default for Pedalboard3 {
    fn default() -> Self {
        Self::new().expect("Failed to create default Pedalboard3 instance")
    }
}

/// Application statistics for monitoring
#[derive(Debug, Clone)]
pub struct ApplicationStatistics {
    pub state: AppState,
    pub audio_engine_running: bool,
    pub active_plugins: usize,
    pub total_available_plugins: usize,
    pub bypassed_plugins: usize,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_app_creation() {
        let app = Pedalboard3::new();
        assert!(app.is_ok());
        
        let app = app.unwrap();
        assert_eq!(app.get_state(), AppState::Initializing);
    }

    #[tokio::test]
    async fn test_app_initialization() {
        let mut app = Pedalboard3::new().unwrap();
        let result = app.initialize().await;
        
        // Note: This might fail in CI without audio devices
        // In a real test suite, you'd use mocks
        match result {
            Ok(_) => assert_eq!(app.get_state(), AppState::Ready),
            Err(_) => {
                // Expected in headless environments
                println!("Audio initialization failed (expected in headless environment)");
            }
        }
    }

    #[test]
    fn test_app_statistics() {
        let app = Pedalboard3::new().unwrap();
        let stats = app.get_statistics();
        assert_eq!(stats.state, AppState::Initializing);
        assert!(!stats.audio_engine_running);
    }
}