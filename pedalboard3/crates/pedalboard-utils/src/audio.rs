//! Audio buffer and processing utilities.

use crate::{AudioResult, AudioError, ChannelConfig, SampleRate, BufferSize};
use bytemuck::{Pod, Zeroable};

/// Real-time safe audio buffer.
#[derive(Debug)]
pub struct AudioBuffer {
    data: Vec<f32>,
    channels: u32,
    frames: usize,
}

impl AudioBuffer {
    /// Create a new audio buffer with specified configuration.
    pub fn new(channels: u32, frames: usize) -> Self {
        Self {
            data: vec![0.0; (channels as usize) * frames],
            channels,
            frames,
        }
    }
    
    /// Get mutable slice for a specific channel.
    pub fn channel_mut(&mut self, channel: u32) -> AudioResult<&mut [f32]> {
        if channel >= self.channels {
            return Err(AudioError::ChannelCountMismatch { 
                expected: self.channels, 
                actual: channel + 1 
            });
        }
        
        let start = (channel as usize) * self.frames;
        let end = start + self.frames;
        Ok(&mut self.data[start..end])
    }
    
    /// Get immutable slice for a specific channel.
    pub fn channel(&self, channel: u32) -> AudioResult<&[f32]> {
        if channel >= self.channels {
            return Err(AudioError::ChannelCountMismatch { 
                expected: self.channels, 
                actual: channel + 1 
            });
        }
        
        let start = (channel as usize) * self.frames;
        let end = start + self.frames;
        Ok(&self.data[start..end])
    }
    
    /// Clear all audio data (set to silence).
    pub fn clear(&mut self) {
        self.data.fill(0.0);
    }
    
    /// Get buffer configuration.
    pub fn config(&self) -> (u32, usize) {
        (self.channels, self.frames)
    }
}

/// Audio format specification.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct AudioFormat {
    pub sample_rate: SampleRate,
    pub channels: ChannelConfig,
    pub buffer_size: BufferSize,
}

impl AudioFormat {
    pub fn new(sample_rate: SampleRate, channels: ChannelConfig, buffer_size: BufferSize) -> Self {
        Self { sample_rate, channels, buffer_size }
    }
}

/// Real-time safe parameter change notification.
#[derive(Debug, Clone, Copy)]
#[repr(C)]
pub struct ParameterChange {
    pub parameter_id: u32,
    pub normalized_value: f32,
}

unsafe impl Pod for ParameterChange {}
unsafe impl Zeroable for ParameterChange {}

/// Lock-free parameter change queue for real-time communication.
pub struct ParameterQueue {
    queue: crossbeam::queue::ArrayQueue<ParameterChange>,
}

impl ParameterQueue {
    /// Create a new parameter queue with specified capacity.
    pub fn new(capacity: usize) -> Self {
        Self {
            queue: crossbeam::queue::ArrayQueue::new(capacity),
        }
    }
    
    /// Push a parameter change (non-blocking).
    pub fn push(&self, change: ParameterChange) -> AudioResult<()> {
        self.queue.push(change).map_err(|_| AudioError::RealTimeViolation)
    }
    
    /// Pop a parameter change (non-blocking).
    pub fn pop(&self) -> Option<ParameterChange> {
        self.queue.pop()
    }
    
    /// Check if queue is empty.
    pub fn is_empty(&self) -> bool {
        self.queue.is_empty()
    }
}