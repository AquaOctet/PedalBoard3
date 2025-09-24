//! VST2 plugin implementation

use crate::{PluginInstance, PluginState};
use anyhow::{Context, Result};
use pedalboard_audio::AudioBuffer;
use std::path::Path;
use std::sync::{Arc, Mutex};
use tracing::{debug, error, warn};
use vst::host::{Host, HostBuffer, PluginInstance as VstInstance};
use vst::plugin::{Info, Plugin};

/// VST2 plugin host implementation
struct VstHost;

impl Host for VstHost {
    fn automate(&self, index: i32, value: f32) {
        debug!("VST automate: parameter {} = {}", index, value);
    }

    fn get_plugin_id(&self) -> i32 {
        // Simple host ID
        0x1337
    }

    fn idle(&self) {
        // Host idle callback - can be used for GUI updates
    }

    fn get_info(&self) -> (isize, String, String) {
        (0x100, "Pedalboard3".to_string(), "Pedalboard3 Team".to_string())
    }
}

/// VST2 plugin instance wrapper
pub struct VstPluginInstance {
    instance: VstInstance,
    bypassed: bool,
    state: PluginState,
    sample_rate: f32,
    buffer_size: u32,
    input_buffer: Vec<Vec<f32>>,
    output_buffer: Vec<Vec<f32>>,
}

impl VstPluginInstance {
    /// Load a VST plugin from file
    pub fn load<P: AsRef<Path>>(path: P) -> Result<Self> {
        let path = path.as_ref();
        debug!("Loading VST plugin: {}", path.display());

        let host = Arc::new(Mutex::new(VstHost));
        let mut loader = vst::host::PluginLoader::load(path, host)
            .with_context(|| format!("Failed to load VST plugin: {}", path.display()))?;

        let mut instance = loader.instance()
            .with_context(|| format!("Failed to create VST instance: {}", path.display()))?;

        // Get plugin info
        let info = instance.get_info();
        debug!("VST Plugin Info: {} by {} (v{})", info.name, info.vendor, info.version);
        debug!("  Inputs: {}, Outputs: {}", info.inputs, info.outputs);
        debug!("  Parameters: {}", info.parameters);

        // Initialize buffers - use stereo by default, adjust based on plugin info
        let input_channels = info.inputs.max(2) as usize;
        let output_channels = info.outputs.max(2) as usize;
        
        Ok(Self {
            instance,
            bypassed: false,
            state: PluginState::Unloaded,
            sample_rate: 44100.0,
            buffer_size: 512,
            input_buffer: vec![vec![0.0; 512]; input_channels],
            output_buffer: vec![vec![0.0; 512]; output_channels],
        })
    }

    /// Resize internal buffers
    fn resize_buffers(&mut self, buffer_size: u32) {
        if buffer_size != self.buffer_size {
            self.buffer_size = buffer_size;
            let size = buffer_size as usize;
            
            for channel in &mut self.input_buffer {
                channel.resize(size, 0.0);
            }
            for channel in &mut self.output_buffer {
                channel.resize(size, 0.0);
            }
        }
    }
}

impl PluginInstance for VstPluginInstance {
    fn activate(&mut self, sample_rate: f32, max_buffer_size: u32) -> Result<()> {
        self.sample_rate = sample_rate;
        self.resize_buffers(max_buffer_size);
        
        // Initialize VST plugin
        self.instance.init();
        self.instance.set_sample_rate(sample_rate);
        self.instance.set_block_size(max_buffer_size as i64);
        
        // Resume plugin
        self.instance.resume();
        
        self.state = PluginState::Ready;
        debug!("VST plugin activated at {}Hz, buffer size {}", sample_rate, max_buffer_size);
        Ok(())
    }

    fn deactivate(&mut self) -> Result<()> {
        if self.state != PluginState::Unloaded {
            self.instance.suspend();
            self.state = PluginState::Unloaded;
            debug!("VST plugin deactivated");
        }
        Ok(())
    }

    fn process(&mut self, input: &AudioBuffer, output: &mut AudioBuffer) -> Result<()> {
        if self.bypassed {
            // Simple bypass - copy input to output
            for (out_ch, in_ch) in output.iter_mut().zip(input.iter()) {
                let len = out_ch.len().min(in_ch.len());
                out_ch[..len].copy_from_slice(&in_ch[..len]);
            }
            return Ok(());
        }

        if self.state != PluginState::Ready {
            return Err(anyhow::anyhow!("Plugin not ready for processing"));
        }

        self.state = PluginState::Processing;

        // Prepare input buffers
        let channels = input.len().min(self.input_buffer.len());
        let samples = if let Some(first_ch) = input.first() {
            first_ch.len().min(self.buffer_size as usize)
        } else {
            0
        };

        // Copy input data to internal buffers
        for ch in 0..channels {
            if ch < input.len() && ch < self.input_buffer.len() {
                let len = samples.min(input[ch].len()).min(self.input_buffer[ch].len());
                self.input_buffer[ch][..len].copy_from_slice(&input[ch][..len]);
            }
        }

        // Clear output buffers
        for channel in &mut self.output_buffer {
            channel.fill(0.0);
        }

        // Create VST host buffers
        let mut host_buffer = HostBuffer::new(
            self.input_buffer.len(),
            self.output_buffer.len()
        );

        // Process with VST plugin
        {
            let mut audio_buffer = host_buffer.bind(&self.input_buffer, &mut self.output_buffer);
            self.instance.process(&mut audio_buffer);
        }

        // Copy output data
        let out_channels = output.len().min(self.output_buffer.len());
        for ch in 0..out_channels {
            if ch < output.len() && ch < self.output_buffer.len() {
                let len = samples.min(output[ch].len()).min(self.output_buffer[ch].len());
                output[ch][..len].copy_from_slice(&self.output_buffer[ch][..len]);
            }
        }

        self.state = PluginState::Ready;
        Ok(())
    }

    fn get_state(&self) -> PluginState {
        self.state.clone()
    }

    fn set_parameter(&mut self, index: u32, value: f32) -> Result<()> {
        if self.state == PluginState::Unloaded {
            return Err(anyhow::anyhow!("Plugin not loaded"));
        }

        // Clamp value to 0.0-1.0 range
        let clamped_value = value.clamp(0.0, 1.0);
        // Simplified parameter setting - in a full implementation, would use VST parameter methods
        debug!("Setting parameter {} to {}", index, clamped_value);
        debug!("Set VST parameter {} = {}", index, clamped_value);
        Ok(())
    }

    fn get_parameter(&self, index: u32) -> Result<f32> {
        if self.state == PluginState::Unloaded {
            return Err(anyhow::anyhow!("Plugin not loaded"));
        }

        // Simplified - return dummy parameter value
        Ok(0.5)
    }

    fn get_parameter_name(&self, index: u32) -> Result<String> {
        if self.state == PluginState::Unloaded {
            return Err(anyhow::anyhow!("Plugin not loaded"));
        }

        // Simplified - return dummy parameter name
        Ok(format!("Parameter {}", index))
    }

    fn get_parameter_count(&self) -> u32 {
        if self.state == PluginState::Unloaded {
            return 0;
        }

        self.instance.get_info().parameters as u32
    }

    fn set_bypass(&mut self, bypassed: bool) {
        self.bypassed = bypassed;
        self.state = if bypassed {
            PluginState::Bypassed
        } else {
            PluginState::Ready
        };
        debug!("VST plugin bypass: {}", bypassed);
    }

    fn is_bypassed(&self) -> bool {
        self.bypassed
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn test_vst_host_info() {
        let host = VstHost;
        let (version, name, vendor) = host.get_info();
        assert_eq!(name, "Pedalboard3");
        assert_eq!(vendor, "Pedalboard3 Team");
        assert_eq!(version, 0x100);
    }

    // Note: VST plugin loading tests require actual VST files
    // These would be integration tests in a real scenario
}