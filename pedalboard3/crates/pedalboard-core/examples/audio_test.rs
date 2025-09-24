//! Test the Pedalboard3 audio engine
//!
//! This example demonstrates the improved audio engine with both
//! mock and real audio capabilities.

use anyhow::Result;
use pedalboard_audio::{AudioEngine, AudioConfig, AudioBuffer};
use std::time::Duration;
use tokio::time::sleep;
use tracing::{info, error};

#[tokio::main]
async fn main() -> Result<()> {
    // Initialize logging
    tracing_subscriber::fmt::init();

    info!("🎵 Pedalboard3 Audio Engine Test");
    info!("=================================");

    // Create and configure audio engine
    let mut engine = AudioEngine::new()?;

    // Configure for common audio settings
    let config = AudioConfig {
        sample_rate: 44100,
        buffer_size: 512,
        channels: 2,
    };
    engine.configure(config)?;

    // Initialize devices (will use mock if no real audio available)
    info!("Initializing audio devices...");
    match engine.initialize_devices() {
        Ok(()) => info!("✅ Audio devices initialized successfully"),
        Err(e) => {
            error!("❌ Failed to initialize devices: {}", e);
            return Err(e);
        }
    }

    // Start audio processing
    info!("Starting audio engine...");
    match engine.start() {
        Ok(()) => info!("✅ Audio engine started successfully"),
        Err(e) => {
            error!("❌ Failed to start audio engine: {}", e);
            return Err(e);
        }
    }

    info!("🔊 Audio Status: {}", if engine.is_running() { "RUNNING" } else { "STOPPED" });

    // Generate some test audio data
    info!("Generating test audio buffers...");
    for i in 0..5 {
        let buffer = generate_test_buffer(512, i as f32 * 0.1);

        if let Err(e) = engine.send_audio_buffer(buffer) {
            info!("Note: Audio buffer not consumed (this is normal in mock mode): {}", e);
        }

        // Wait a bit between buffers
        sleep(Duration::from_millis(100)).await;
        info!("Sent test buffer #{}", i + 1);
    }

    info!("Running audio engine for 3 seconds...");
    sleep(Duration::from_secs(3)).await;

    // Stop the engine
    info!("Stopping audio engine...");
    engine.stop()?;
    info!("✅ Audio engine stopped successfully");

    info!("🎵 Audio test completed!");
    Ok(())
}

/// Generate a test audio buffer with a simple sine wave
fn generate_test_buffer(frames: usize, frequency_multiplier: f32) -> AudioBuffer {
    let sample_rate = 44100.0;
    let frequency = 440.0 * (1.0 + frequency_multiplier); // A4 with variation
    let mut buffer = Vec::with_capacity(frames);

    for frame_idx in 0..frames {
        let t = frame_idx as f32 / sample_rate;
        let sample = (t * frequency * 2.0 * std::f32::consts::PI).sin() * 0.1;

        // Create stereo frame
        buffer.push(vec![sample, sample]);
    }

    buffer
}