//! Test the Pedalboard3 plugin scanning system
//!
//! This example demonstrates the plugin scanner finding and cataloging
//! available VST/AU/CLAP plugins on the system.

use anyhow::Result;
use pedalboard_plugins::{PluginManager, PluginFormat, PluginScanner};
use tracing::info;

#[tokio::main]
async fn main() -> Result<()> {
    // Initialize logging
    tracing_subscriber::fmt::init();

    info!("🔌 Pedalboard3 Plugin Scanner Test");
    info!("===================================");

    // Create plugin manager
    let mut plugin_manager = PluginManager::new()?;
    info!("✅ Plugin manager created");

    // Test scanner with custom path
    info!("Setting up plugin scanner...");
    let mut scanner = PluginScanner::new();

    // Add a test directory with mock plugins
    scanner.add_search_path("/tmp/test_vst_plugins");

    // Show configured search paths
    info!("📁 Configured search paths:");
    for (i, path) in scanner.get_search_paths().iter().enumerate() {
        if path.exists() {
            info!("  {}. {} ✅", i + 1, path.display());
        } else {
            info!("  {}. {} (not found)", i + 1, path.display());
        }
    }

    // Perform plugin scan
    info!("🔍 Starting plugin scan...");
    let scan_result = plugin_manager.scan_plugins();

    match scan_result {
        Ok(plugin_count) => {
            info!("✅ Plugin scan completed!");
            info!("📊 Found {} plugins total", plugin_count);

            // Get available plugins
            let plugins = plugin_manager.get_available_plugins();

            if plugins.is_empty() {
                info!("ℹ️  No plugins found. This is normal if no VST plugins are installed.");
            } else {
                info!("📋 Plugin Details:");
                for (i, plugin) in plugins.iter().enumerate() {
                    info!("  {}. {} ({})", i + 1, plugin.name, plugin.format.format_name());
                    info!("     Path: {}", plugin.path.display());
                    info!("     Vendor: {}, Version: {}", plugin.vendor, plugin.version);
                    info!("     I/O: {}ch → {}ch", plugin.input_channels, plugin.output_channels);
                    info!("     Features: Editor={}, MIDI In={}, MIDI Out={}",
                           plugin.has_editor, plugin.can_receive_midi, plugin.can_send_midi);
                }
            }

            // Show breakdown by format
            info!("📈 Plugin Format Breakdown:");
            let formats = [
                PluginFormat::VST2,
                PluginFormat::VST3,
                PluginFormat::AU,
                PluginFormat::CLAP,
                PluginFormat::LV2,
            ];

            for format in formats {
                let format_plugins = plugin_manager.get_plugins_by_format(format.clone());
                info!("  {}: {} plugins", format.format_name(), format_plugins.len());

                for plugin in format_plugins.iter().take(3) {  // Show first 3
                    info!("    • {}", plugin.name);
                }
                if format_plugins.len() > 3 {
                    info!("    • ... and {} more", format_plugins.len() - 3);
                }
            }
        },
        Err(e) => {
            info!("❌ Plugin scan failed: {}", e);
            return Err(e);
        }
    }

    // Test individual scanner functions
    info!("🧪 Testing scanner features...");

    // Test quick scan
    let quick_result = scanner.quick_scan();
    match quick_result {
        Ok(plugins) => {
            info!("⚡ Quick scan found {} plugins", plugins.len());
        },
        Err(e) => {
            info!("⚡ Quick scan failed: {}", e);
        }
    }

    // Test format-specific scanning
    for format in [PluginFormat::VST2, PluginFormat::VST3] {
        let format_result = scanner.scan_format(format.clone());
        match format_result {
            Ok(plugins) => {
                info!("🎯 {} scan found {} plugins", format.format_name(), plugins.len());
            },
            Err(e) => {
                info!("🎯 {} scan failed: {}", format.format_name(), e);
            }
        }
    }

    info!("🔌 Plugin scanner test completed!");
    Ok(())
}