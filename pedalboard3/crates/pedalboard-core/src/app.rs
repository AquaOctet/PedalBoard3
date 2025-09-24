//! Application utilities and helpers

use anyhow::Result;
use std::sync::Arc;
use tokio::sync::Mutex;
use tracing::{info, warn};

/// Application builder for configuring the Pedalboard3 instance
pub struct Pedalboard3Builder {
    config: Option<crate::ApplicationConfig>,
}

impl Pedalboard3Builder {
    /// Create a new builder
    pub fn new() -> Self {
        Self { config: None }
    }

    /// Set custom configuration
    pub fn with_config(mut self, config: crate::ApplicationConfig) -> Self {
        self.config = Some(config);
        self
    }

    /// Load configuration from file
    pub fn with_config_file<P: AsRef<std::path::Path>>(mut self, path: P) -> Result<Self> {
        let config = crate::ApplicationConfig::load(path)?;
        self.config = Some(config);
        Ok(self)
    }

    /// Build the Pedalboard3 application
    pub fn build(self) -> Result<crate::Pedalboard3> {
        match self.config {
            Some(config) => {
                // Create app with custom config
                let mut app = crate::Pedalboard3::new()?;
                // Note: In a full implementation, you'd need to inject the config
                // For now, we'll use the default behavior
                Ok(app)
            }
            None => crate::Pedalboard3::new(),
        }
    }
}

impl Default for Pedalboard3Builder {
    fn default() -> Self {
        Self::new()
    }
}

/// Initialize logging for the application
pub fn init_logging() -> Result<()> {
    use tracing_subscriber::{fmt, EnvFilter, prelude::*};

    let env_filter = EnvFilter::try_from_default_env()
        .or_else(|_| EnvFilter::try_new("pedalboard3=info"))
        .unwrap();

    tracing_subscriber::registry()
        .with(fmt::layer()
            .with_target(false)
            .with_thread_ids(false)
            .with_thread_names(false))
        .with(env_filter)
        .init();

    info!("Logging initialized");
    Ok(())
}

/// Setup signal handlers for graceful shutdown
pub async fn setup_signal_handlers() -> Result<tokio::sync::oneshot::Receiver<()>> {
    let (shutdown_tx, shutdown_rx) = tokio::sync::oneshot::channel();

    tokio::spawn(async move {
        match tokio::signal::ctrl_c().await {
            Ok(()) => {
                info!("Received Ctrl+C signal, initiating shutdown");
                let _ = shutdown_tx.send(());
            }
            Err(err) => {
                warn!("Failed to listen for Ctrl+C signal: {}", err);
            }
        }
    });

    Ok(shutdown_rx)
}

/// Run the application with proper initialization and cleanup
pub async fn run_application() -> Result<()> {
    // Initialize logging
    init_logging()?;
    
    info!("Starting Pedalboard3 application");

    // Setup signal handlers
    let mut shutdown_rx = setup_signal_handlers().await?;

    // Create and initialize application
    let mut app = Pedalboard3Builder::new().build()?;
    app.initialize().await?;

    // Start the application in a separate task
    let app = Arc::new(Mutex::new(app));
    let app_handle = {
        let app_clone = Arc::clone(&app);
        tokio::spawn(async move {
            let mut app = app_clone.lock().await;
            if let Err(e) = app.start().await {
                eprintln!("Application error: {}", e);
            }
        })
    };

    // Wait for shutdown signal
    tokio::select! {
        _ = &mut shutdown_rx => {
            info!("Shutdown signal received");
        }
        _ = app_handle => {
            info!("Application task completed");
        }
    }

    // Stop the application
    {
        let mut app = app.lock().await;
        app.stop().await?;
    }

    info!("Pedalboard3 application shutdown complete");
    Ok(())
}

/// Validate system requirements
pub fn validate_system() -> Result<()> {
    info!("Validating system requirements");

    // Check for audio system availability
    let device_manager = pedalboard_audio::DeviceManager::new();
    if device_manager.default_output_device().is_some() {
        info!("Audio output device available");
    } else {
        warn!("No audio output device found - running in headless mode");
    }

    // Check available memory (basic check)
    // In a real implementation, you might use a crate like `sysinfo`
    
    // Check disk space for configuration directories
    let config_dir = crate::ApplicationConfig::default().paths.config_dir;
    if let Some(parent) = config_dir.parent() {
        if !parent.exists() {
            warn!("Configuration directory parent does not exist: {}", parent.display());
        }
    }

    info!("System validation complete");
    Ok(())
}

/// Application panic handler
pub fn setup_panic_handler() {
    std::panic::set_hook(Box::new(|panic_info| {
        let msg = match panic_info.payload().downcast_ref::<&'static str>() {
            Some(s) => *s,
            None => match panic_info.payload().downcast_ref::<String>() {
                Some(s) => &s[..],
                None => "Box<Any>",
            }
        };

        let location = panic_info.location().map(|l| format!(" at {}:{}", l.file(), l.line())).unwrap_or_default();
        
        eprintln!("PANIC: {}{}", msg, location);
        
        // In a production application, you might want to:
        // 1. Save current session
        // 2. Log crash report
        // 3. Show user-friendly error dialog
        // 4. Attempt graceful cleanup
    }));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_builder_creation() {
        let builder = Pedalboard3Builder::new();
        assert!(builder.config.is_none());
    }

    #[test]
    fn test_builder_with_config() {
        let config = crate::ApplicationConfig::default();
        let builder = Pedalboard3Builder::new().with_config(config);
        assert!(builder.config.is_some());
    }

    #[tokio::test]
    async fn test_signal_handler_setup() {
        let shutdown_rx = setup_signal_handlers().await;
        assert!(shutdown_rx.is_ok());
        
        // Note: We can't easily test the actual signal handling in a unit test
        // without sending actual signals, which would be disruptive
    }

    #[test]
    fn test_system_validation() {
        // This test might fail in headless CI environments, but shouldn't panic
        let result = validate_system();
        assert!(result.is_ok());
    }

    #[test]
    fn test_panic_handler_setup() {
        // Just verify we can set up the panic handler without issues
        setup_panic_handler();
        
        // The actual panic handling can't be easily unit tested
        // since it would terminate the test process
    }
}