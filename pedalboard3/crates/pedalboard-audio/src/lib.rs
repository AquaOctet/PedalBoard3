//! Pedalboard3 Audio Engine
//!
//! Real-time audio processing engine with optional CPAL-based I/O

use anyhow::{Context, Result};
#[cfg(feature = "cpal")]
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
#[cfg(feature = "cpal")]
use cpal::{Device, Host, Stream, StreamConfig};
use crossbeam::channel::{Receiver, Sender};
use std::sync::Arc;
use tracing::{debug, error, info, warn};

pub mod buffer;
pub mod device;
pub mod graph;
pub mod processor;

pub use buffer::*;
pub use device::*;
pub use graph::*;
pub use processor::*;

/// Audio sample type - f32 for optimal SIMD performance
pub type Sample = f32;

/// Audio frame represents a single sample across all channels
pub type Frame = Vec<Sample>;

/// Audio buffer for processing blocks
pub type AudioBuffer = Vec<Frame>;

/// Audio configuration
#[derive(Debug, Clone)]
pub struct AudioConfig {
    pub sample_rate: u32,
    pub buffer_size: u32,
    pub channels: u32,
}

impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            sample_rate: 44100,
            buffer_size: 512,
            channels: 2,
        }
    }
}

/// Main audio engine that coordinates I/O and processing
pub struct AudioEngine {
    config: AudioConfig,
    #[cfg(feature = "cpal")]
    host: Host,
    #[cfg(feature = "cpal")]
    input_device: Option<Device>,
    #[cfg(feature = "cpal")]
    output_device: Option<Device>,
    #[cfg(feature = "cpal")]
    stream: Option<Stream>,
    processor_tx: Option<Sender<AudioBuffer>>,
    processor_rx: Option<Receiver<AudioBuffer>>,
    #[cfg(not(feature = "cpal"))]
    running: bool,
}

impl AudioEngine {
    /// Create a new audio engine
    pub fn new() -> Result<Self> {
        #[cfg(feature = "cpal")]
        {
            let host = cpal::default_host();
            info!("Audio engine initialized with host: {}", host.id().name());

            Ok(Self {
                config: AudioConfig::default(),
                host,
                input_device: None,
                output_device: None,
                stream: None,
                processor_tx: None,
                processor_rx: None,
            })
        }
        
        #[cfg(not(feature = "cpal"))]
        {
            info!("Audio engine initialized in mock mode (no audio hardware required)");
            Ok(Self {
                config: AudioConfig::default(),
                processor_tx: None,
                processor_rx: None,
                running: false,
            })
        }
    }

    /// Configure audio settings
    pub fn configure(&mut self, config: AudioConfig) -> Result<()> {
        self.config = config;
        info!("Audio configured: {:?}", self.config);
        Ok(())
    }

    /// Initialize audio devices
    pub fn initialize_devices(&mut self) -> Result<()> {
        #[cfg(feature = "cpal")]
        {
            match self.host.default_input_device() {
                Some(device) => {
                    let device_name = device.name().unwrap_or_else(|_| "Unknown".to_string());
                    info!("Input device: {}", device_name);
                    self.input_device = Some(device);
                },
                None => {
                    warn!("No input device available - input will be disabled");
                }
            }

            match self.host.default_output_device() {
                Some(device) => {
                    let device_name = device.name().unwrap_or_else(|_| "Unknown".to_string());
                    info!("Output device: {}", device_name);
                    self.output_device = Some(device);
                },
                None => {
                    warn!("No output device available - falling back to mock audio");
                    return self.initialize_mock_devices();
                }
            }
        }

        #[cfg(not(feature = "cpal"))]
        {
            self.initialize_mock_devices()?;
        }

        Ok(())
    }

    /// Initialize mock audio devices
    fn initialize_mock_devices(&mut self) -> Result<()> {
        info!("Mock audio devices initialized:");
        info!("  Input device: Mock Input Device (generates silence)");
        info!("  Output device: Mock Output Device (test tone generator)");
        Ok(())
    }

    /// Start audio processing
    pub fn start(&mut self) -> Result<()> {
        // First try CPAL if available
        #[cfg(feature = "cpal")]
        {
            if let Some(output_device) = self.output_device.as_ref() {
                // Try to start real audio
                if let Ok(()) = self.start_real_audio(output_device) {
                    return Ok(());
                } else {
                    warn!("Failed to start real audio, falling back to mock audio");
                }
            }
        }

        // Fall back to mock audio
        self.start_mock_audio()
    }

    /// Start real audio processing with CPAL
    #[cfg(feature = "cpal")]
    fn start_real_audio(&mut self, output_device: &Device) -> Result<()> {
        // Create processing channels for communication with processing thread
        let (tx, rx) = crossbeam::channel::bounded(4);
        self.processor_tx = Some(tx);
        self.processor_rx = Some(rx.clone());

        // Build stream configuration
        let stream_config = StreamConfig {
            channels: self.config.channels as cpal::ChannelCount,
            sample_rate: cpal::SampleRate(self.config.sample_rate),
            buffer_size: cpal::BufferSize::Fixed(self.config.buffer_size),
        };

        info!("Starting real audio stream with config: {:?}", stream_config);

        // Build output stream
        let stream = output_device.build_output_stream(
            &stream_config,
            move |data: &mut [f32], _: &cpal::OutputCallbackInfo| {
                Self::audio_callback_realtime(data, &rx);
            },
            |err| error!("Audio stream error: {}", err),
            None,
        ).context("Failed to build output stream")?;

        stream.play().context("Failed to start audio stream")?;
        self.stream = Some(stream);

        info!("Real audio engine started successfully");
        Ok(())
    }

    /// Start mock audio processing
    fn start_mock_audio(&mut self) -> Result<()> {
        #[cfg(not(feature = "cpal"))]
        {
            self.running = true;
        }

        // Create channels for mock processing
        let (tx, rx) = crossbeam::channel::bounded(4);
        self.processor_tx = Some(tx);
        self.processor_rx = Some(rx);

        info!("Mock audio engine started successfully");
        info!("Simulating audio processing with config: {:?}", self.config);

        // Start a background thread to simulate audio processing
        let config = self.config.clone();
        std::thread::spawn(move || {
            let mut sample_counter = 0u64;
            loop {
                std::thread::sleep(std::time::Duration::from_millis(
                    (config.buffer_size * 1000 / config.sample_rate) as u64
                ));
                sample_counter += config.buffer_size as u64;

                // Every ~1 second, log simulated processing
                if sample_counter % config.sample_rate as u64 == 0 {
                    debug!("Mock audio: processed {} samples ({}s runtime)",
                           sample_counter, sample_counter / config.sample_rate as u64);
                }
            }
        });

        Ok(())
    }

    /// Stop audio processing
    pub fn stop(&mut self) -> Result<()> {
        #[cfg(feature = "cpal")]
        {
            if let Some(stream) = self.stream.take() {
                drop(stream);
                info!("Audio stream stopped");
            }
        }
        
        #[cfg(not(feature = "cpal"))]
        {
            self.running = false;
            info!("Mock audio engine stopped");
        }
        
        Ok(())
    }

    /// Real-time audio callback for processing
    #[cfg(feature = "cpal")]
    fn audio_callback_realtime(output: &mut [f32], rx: &Receiver<AudioBuffer>) {
        // Create audio buffer from output slice
        let channels = 2; // Assume stereo for now
        let frames = output.len() / channels;

        // Initialize with silence
        for sample in output.iter_mut() {
            *sample = 0.0;
        }

        // Try to get processed audio from the processing graph
        if let Ok(audio_buffer) = rx.try_recv() {
            // Convert AudioBuffer to interleaved output
            for (frame_idx, frame) in audio_buffer.iter().take(frames).enumerate() {
                for (ch_idx, &sample) in frame.iter().take(channels).enumerate() {
                    let output_idx = frame_idx * channels + ch_idx;
                    if output_idx < output.len() {
                        output[output_idx] = sample;
                    }
                }
            }
        } else {
            // Generate a simple test tone when no processed audio available
            static mut PHASE: f32 = 0.0;
            let sample_rate = 44100.0;
            let frequency = 440.0; // A4
            let amplitude = 0.05; // Quieter test tone

            unsafe {
                for (i, sample) in output.iter_mut().enumerate() {
                    if i % channels == 0 {
                        // Only calculate for left channel, duplicate for right
                        *sample = (PHASE * frequency * 2.0 * std::f32::consts::PI).sin() * amplitude;
                        PHASE += 1.0 / sample_rate;
                        if PHASE >= 1.0 {
                            PHASE -= 1.0;
                        }
                    } else {
                        // Copy left channel to right channel
                        *sample = output[i - 1];
                    }
                }
            }
        }
    }

    /// Send audio buffer to processing queue
    pub fn send_audio_buffer(&self, buffer: AudioBuffer) -> Result<()> {
        if let Some(ref tx) = self.processor_tx {
            tx.try_send(buffer).map_err(|e| anyhow::anyhow!("Failed to send audio buffer: {}", e))?;
        }
        Ok(())
    }

    /// Get available audio devices
    #[cfg(feature = "cpal")]
    pub fn get_available_devices(&self) -> Result<Vec<cpal::Device>> {
        let input_devices: Vec<_> = self.host.input_devices()?.collect();
        let output_devices: Vec<_> = self.host.output_devices()?.collect();

        let mut all_devices = input_devices;
        all_devices.extend(output_devices);
        Ok(all_devices)
    }

    /// Get available audio devices (mock version)
    #[cfg(not(feature = "cpal"))]
    pub fn get_available_devices(&self) -> Result<Vec<String>> {
        Ok(vec![
            "Mock Input Device".to_string(),
            "Mock Output Device".to_string(),
        ])
    }

    /// Get current configuration
    pub fn config(&self) -> &AudioConfig {
        &self.config
    }

    /// Check if engine is running
    pub fn is_running(&self) -> bool {
        #[cfg(feature = "cpal")]
        {
            self.stream.is_some()
        }
        
        #[cfg(not(feature = "cpal"))]
        {
            self.running
        }
    }
}

impl Default for AudioEngine {
    fn default() -> Self {
        Self::new().expect("Failed to create default audio engine")
    }
}

impl Drop for AudioEngine {
    fn drop(&mut self) {
        let _ = self.stop();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_audio_engine_creation() {
        let engine = AudioEngine::new();
        assert!(engine.is_ok());
    }

    #[test]
    fn test_audio_config_default() {
        let config = AudioConfig::default();
        assert_eq!(config.sample_rate, 44100);
        assert_eq!(config.buffer_size, 512);
        assert_eq!(config.channels, 2);
    }

    #[test]
    fn test_engine_configuration() {
        let mut engine = AudioEngine::new().unwrap();
        let config = AudioConfig {
            sample_rate: 48000,
            buffer_size: 1024,
            channels: 4,
        };
        
        let result = engine.configure(config.clone());
        assert!(result.is_ok());
        assert_eq!(engine.config().sample_rate, 48000);
    }
}