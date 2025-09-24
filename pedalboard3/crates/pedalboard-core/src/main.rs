//! Pedalboard3 - Modern Audio Plugin Host
//!
//! Main application entry point

use anyhow::Result;
use clap::{Arg, Command};
use pedalboard_core::{app::*, ApplicationConfig, Pedalboard3};
use std::path::PathBuf;
use tracing::{error, info};

#[tokio::main]
async fn main() -> Result<()> {
    // Setup panic handler first
    setup_panic_handler();

    let matches = Command::new("pedalboard3")
        .version("3.0.0")
        .author("Pedalboard3 Development Team")
        .about("Modern audio plugin host for live performance")
        .arg(Arg::new("config")
            .short('c')
            .long("config")
            .value_name("FILE")
            .help("Sets a custom config file")
            .value_parser(clap::value_parser!(PathBuf)))
        .arg(Arg::new("session")
            .short('s')
            .long("session")
            .value_name("FILE")
            .help("Load a session file on startup")
            .value_parser(clap::value_parser!(PathBuf)))
        .arg(Arg::new("headless")
            .long("headless")
            .help("Run without GUI (audio processing only)")
            .action(clap::ArgAction::SetTrue))
        .arg(Arg::new("validate")
            .long("validate")
            .help("Validate system and exit")
            .action(clap::ArgAction::SetTrue))
        .arg(Arg::new("list-devices")
            .long("list-devices")
            .help("List audio devices and exit")
            .action(clap::ArgAction::SetTrue))
        .arg(Arg::new("scan-plugins")
            .long("scan-plugins")
            .help("Scan for plugins and exit")
            .action(clap::ArgAction::SetTrue))
        .arg(Arg::new("verbose")
            .short('v')
            .long("verbose")
            .help("Enable verbose logging")
            .action(clap::ArgAction::Count))
        .get_matches();

    // Setup logging based on verbosity
    setup_logging(matches.get_count("verbose"))?;

    info!("Pedalboard3 v3.0.0 starting");

    // Handle special commands that don't require full app startup
    if matches.get_flag("validate") {
        return validate_and_exit().await;
    }

    if matches.get_flag("list-devices") {
        return list_devices_and_exit().await;
    }

    if matches.get_flag("scan-plugins") {
        return scan_plugins_and_exit().await;
    }

    // Create application
    let mut app = if let Some(config_path) = matches.get_one::<PathBuf>("config") {
        info!("Loading config from: {}", config_path.display());
        Pedalboard3Builder::new()
            .with_config_file(config_path)?
            .build()?
    } else {
        Pedalboard3Builder::new().build()?
    };

    // Initialize application
    app.initialize().await?;

    // Load session if specified
    if let Some(session_path) = matches.get_one::<PathBuf>("session") {
        info!("Loading session from: {}", session_path.display());
        app.load_session(session_path).await?;
    }

    // Determine run mode
    if matches.get_flag("headless") {
        info!("Running in headless mode (no GUI)");
        run_headless_mode(app).await?;
    } else {
        info!("Running in full mode with GUI");
        run_full_mode(app).await?;
    }

    Ok(())
}

/// Setup logging based on verbosity level
fn setup_logging(verbosity: u8) -> Result<()> {
    use tracing_subscriber::{fmt, EnvFilter, prelude::*};

    let log_level = match verbosity {
        0 => "pedalboard3=info",
        1 => "pedalboard3=debug",
        2 => "pedalboard3=trace",
        _ => "trace",
    };

    let env_filter = EnvFilter::try_from_default_env()
        .or_else(|_| EnvFilter::try_new(log_level))?;

    tracing_subscriber::registry()
        .with(fmt::layer()
            .with_target(verbosity > 1)
            .with_thread_ids(verbosity > 2)
            .with_thread_names(verbosity > 2)
            .with_file(verbosity > 2)
            .with_line_number(verbosity > 2))
        .with(env_filter)
        .init();

    Ok(())
}

/// Validate system and exit
async fn validate_and_exit() -> Result<()> {
    println!("Validating Pedalboard3 system requirements...");
    
    validate_system()?;
    
    // Check configuration
    let config = ApplicationConfig::load_or_default()?;
    config.validate()?;
    println!("Configuration valid");
    
    // Ensure directories exist
    config.ensure_directories()?;
    println!("Directories validated");
    
    println!("✓ System validation complete - Pedalboard3 should run normally");
    Ok(())
}

/// List audio devices and exit
async fn list_devices_and_exit() -> Result<()> {
    use pedalboard_audio::DeviceManager;
    
    println!("Available Audio Devices:");
    println!("========================");
    
    let device_manager = DeviceManager::new();
    let devices = device_manager.list_devices()?;
    
    if devices.is_empty() {
        println!("No audio devices found");
        return Ok(());
    }
    
    for (i, device) in devices.iter().enumerate() {
        println!("{}. {}", i + 1, device);
        // Device info is now formatted in the Display trait, no need to print details separately
    }
    
    println!("\nHost: {}", device_manager.host_name());
    Ok(())
}

/// Scan for plugins and exit
async fn scan_plugins_and_exit() -> Result<()> {
    use pedalboard_plugins::PluginScanner;
    
    println!("Scanning for audio plugins...");
    println!("=============================");
    
    let scanner = PluginScanner::new();
    println!("Search paths:");
    for path in scanner.get_search_paths() {
        println!("  {}", path.display());
    }
    
    println!("\nScanning...");
    let plugins = scanner.scan_all()?;
    
    if plugins.is_empty() {
        println!("No plugins found");
        return Ok(());
    }
    
    // Group by format
    let mut vst2_count = 0;
    let mut vst3_count = 0;
    let mut au_count = 0;
    let mut clap_count = 0;
    let mut lv2_count = 0;
    
    for plugin in &plugins {
        match plugin.format {
            pedalboard_plugins::PluginFormat::VST2 => vst2_count += 1,
            pedalboard_plugins::PluginFormat::VST3 => vst3_count += 1,
            pedalboard_plugins::PluginFormat::AU => au_count += 1,
            pedalboard_plugins::PluginFormat::CLAP => clap_count += 1,
            pedalboard_plugins::PluginFormat::LV2 => lv2_count += 1,
        }
        
        println!("{} - {} ({})", plugin.name, plugin.vendor, plugin.format.format_name());
    }
    
    println!("\nSummary:");
    println!("  VST2: {}", vst2_count);
    println!("  VST3: {}", vst3_count);
    println!("  AU:   {}", au_count);
    println!("  CLAP: {}", clap_count);
    println!("  LV2:  {}", lv2_count);
    println!("  Total: {}", plugins.len());
    
    Ok(())
}

/// Run in headless mode (audio processing only)
async fn run_headless_mode(mut app: Pedalboard3) -> Result<()> {
    info!("Starting headless audio processing");
    
    // Setup signal handlers
    let mut shutdown_rx = setup_signal_handlers().await?;
    
    // Start audio processing
    tokio::select! {
        result = app.start() => {
            match result {
                Ok(_) => info!("Audio processing completed normally"),
                Err(e) => error!("Audio processing error: {}", e),
            }
        }
        _ = &mut shutdown_rx => {
            info!("Shutdown signal received");
            app.stop().await?;
        }
    }
    
    Ok(())
}

/// Run in full mode with GUI
async fn run_full_mode(app: Pedalboard3) -> Result<()> {
    info!("Starting full application with GUI");
    
    // For now, just run the headless mode
    // In a full implementation, this would start the GUI
    println!("GUI mode not yet implemented - running headless");
    println!("Press Ctrl+C to exit");
    
    run_headless_mode(app).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_logging_setup() {
        // Test different verbosity levels
        assert!(setup_logging(0).is_ok());
    }
}