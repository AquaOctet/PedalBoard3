//! Audio processor implementations and effects

use crate::{AudioBuffer, Sample};
use anyhow::Result;
use std::f32::consts::PI;

/// Basic audio effects and processors
pub struct AudioProcessor;

impl AudioProcessor {
    /// Apply gain to audio buffer
    pub fn gain(buffer: &mut AudioBuffer, gain_db: f32) {
        let gain_linear = Self::db_to_linear(gain_db);
        for channel in buffer.iter_mut() {
            for sample in channel.iter_mut() {
                *sample *= gain_linear;
            }
        }
    }

    /// Apply simple low-pass filter
    pub fn low_pass_filter(buffer: &mut AudioBuffer, cutoff_hz: f32, sample_rate: f32, state: &mut Vec<f32>) {
        if state.len() != buffer.len() {
            state.resize(buffer.len(), 0.0);
        }

        let rc = 1.0 / (2.0 * PI * cutoff_hz);
        let dt = 1.0 / sample_rate;
        let alpha = dt / (rc + dt);

        for (ch, channel) in buffer.iter_mut().enumerate() {
            for sample in channel.iter_mut() {
                state[ch] = state[ch] + alpha * (*sample - state[ch]);
                *sample = state[ch];
            }
        }
    }

    /// Apply simple high-pass filter
    pub fn high_pass_filter(buffer: &mut AudioBuffer, cutoff_hz: f32, sample_rate: f32, state: &mut Vec<(f32, f32)>) {
        if state.len() != buffer.len() {
            state.resize(buffer.len(), (0.0, 0.0));
        }

        let rc = 1.0 / (2.0 * PI * cutoff_hz);
        let dt = 1.0 / sample_rate;
        let alpha = rc / (rc + dt);

        for (ch, channel) in buffer.iter_mut().enumerate() {
            for sample in channel.iter_mut() {
                let (prev_input, prev_output) = &mut state[ch];
                let output = alpha * (*prev_output + *sample - *prev_input);
                *prev_input = *sample;
                *prev_output = output;
                *sample = output;
            }
        }
    }

    /// Apply delay effect
    pub fn delay(buffer: &mut AudioBuffer, delay_samples: usize, feedback: f32, mix: f32, delay_buffer: &mut Vec<Vec<f32>>) {
        if delay_buffer.len() != buffer.len() {
            delay_buffer.resize(buffer.len(), vec![0.0; delay_samples.max(1024)]);
        }

        for (ch, channel) in buffer.iter_mut().enumerate() {
            let delay_line = &mut delay_buffer[ch];
            
            for (i, sample) in channel.iter_mut().enumerate() {
                let delay_pos = i % delay_line.len();
                let delayed_sample = delay_line[delay_pos];
                
                // Apply feedback
                delay_line[delay_pos] = *sample + delayed_sample * feedback;
                
                // Mix dry and wet signals
                *sample = *sample * (1.0 - mix) + delayed_sample * mix;
            }
        }
    }

    /// Apply distortion/overdrive
    pub fn distortion(buffer: &mut AudioBuffer, drive: f32, level: f32) {
        let gain = Self::db_to_linear(drive);
        let output_level = Self::db_to_linear(level);
        
        for channel in buffer.iter_mut() {
            for sample in channel.iter_mut() {
                // Apply drive
                let driven = *sample * gain;
                
                // Soft clipping using tanh
                let distorted = driven.tanh();
                
                // Apply output level
                *sample = distorted * output_level;
            }
        }
    }

    /// Simple reverb using multiple delay lines
    pub fn reverb(buffer: &mut AudioBuffer, room_size: f32, damping: f32, mix: f32, state: &mut ReverbState) {
        if state.delay_lines.len() != buffer.len() {
            state.init(buffer.len());
        }

        let feedback = room_size * 0.84;
        
        for (ch, channel) in buffer.iter_mut().enumerate() {
            let delay_lines = &mut state.delay_lines[ch];
            let low_pass_state = &mut state.low_pass_states[ch];
            
            for sample in channel.iter_mut() {
                let mut reverb_sum = 0.0;
                
                // Process each delay line
                for (i, delay_line) in delay_lines.iter_mut().enumerate() {
                    let delay_pos = state.positions[ch][i];
                    let delayed = delay_line[delay_pos];
                    
                    // Apply damping (low-pass filter)
                    let damped = delayed * (1.0 - damping) + low_pass_state[i] * damping;
                    low_pass_state[i] = damped;
                    
                    // Feedback
                    delay_line[delay_pos] = *sample * 0.2 + damped * feedback;
                    
                    // Advance position
                    state.positions[ch][i] = (state.positions[ch][i] + 1) % delay_line.len();
                    
                    reverb_sum += damped;
                }
                
                reverb_sum /= delay_lines.len() as f32;
                *sample = *sample * (1.0 - mix) + reverb_sum * mix;
            }
        }
    }

    /// Convert decibels to linear scale
    pub fn db_to_linear(db: f32) -> f32 {
        10.0_f32.powf(db / 20.0)
    }

    /// Convert linear scale to decibels
    pub fn linear_to_db(linear: f32) -> f32 {
        20.0 * linear.abs().max(1e-10).log10()
    }
}

/// State for reverb effect
pub struct ReverbState {
    delay_lines: Vec<Vec<Vec<f32>>>,
    low_pass_states: Vec<Vec<f32>>,
    positions: Vec<Vec<usize>>,
}

impl ReverbState {
    pub fn new() -> Self {
        Self {
            delay_lines: Vec::new(),
            low_pass_states: Vec::new(),
            positions: Vec::new(),
        }
    }

    fn init(&mut self, channels: usize) {
        // Delay line lengths for reverb (different prime numbers for diffusion)
        let delay_lengths = [347, 383, 431, 479, 523, 569, 617, 661];
        
        self.delay_lines.clear();
        self.low_pass_states.clear();
        self.positions.clear();
        
        for _ in 0..channels {
            let mut channel_delays = Vec::new();
            let mut channel_states = Vec::new();
            let mut channel_positions = Vec::new();
            
            for &length in &delay_lengths {
                channel_delays.push(vec![0.0; length]);
                channel_states.push(0.0);
                channel_positions.push(0);
            }
            
            self.delay_lines.push(channel_delays);
            self.low_pass_states.push(channel_states);
            self.positions.push(channel_positions);
        }
    }
}

impl Default for ReverbState {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_db_conversion() {
        let db = 6.0;
        let linear = AudioProcessor::db_to_linear(db);
        let back_to_db = AudioProcessor::linear_to_db(linear);
        assert!((db - back_to_db).abs() < 0.001);
    }

    #[test]
    fn test_gain_effect() {
        let mut buffer = vec![vec![1.0, -1.0, 0.5, -0.5]];
        AudioProcessor::gain(&mut buffer, 6.0);
        
        // 6dB should be approximately 2x gain
        assert!((buffer[0][0] - 2.0).abs() < 0.1);
        assert!((buffer[0][1] - (-2.0)).abs() < 0.1);
    }

    #[test]
    fn test_reverb_state_creation() {
        let mut state = ReverbState::new();
        state.init(2);
        assert_eq!(state.delay_lines.len(), 2);
        assert_eq!(state.low_pass_states.len(), 2);
        assert_eq!(state.positions.len(), 2);
    }
}