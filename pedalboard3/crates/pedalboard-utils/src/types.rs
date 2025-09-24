//! Core types and traits for Pedalboard3.

use serde::{Deserialize, Serialize};
use uuid::Uuid;

/// Unique identifier for audio processing nodes.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct NodeId(pub Uuid);

impl NodeId {
    /// Create a new random NodeId.
    pub fn new() -> Self {
        Self(Uuid::new_v4())
    }
}

impl Default for NodeId {
    fn default() -> Self {
        Self::new()
    }
}

/// Sample rate in Hz.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SampleRate(pub f64);

impl SampleRate {
    pub const CD_QUALITY: Self = Self(44100.0);
    pub const HIGH_QUALITY: Self = Self(48000.0);
    pub const PROFESSIONAL: Self = Self(96000.0);
    
    pub fn as_f64(self) -> f64 {
        self.0
    }
    
    pub fn as_u32(self) -> u32 {
        self.0 as u32
    }
}

/// Buffer size in samples.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct BufferSize(pub usize);

impl BufferSize {
    pub const ULTRA_LOW_LATENCY: Self = Self(32);
    pub const LOW_LATENCY: Self = Self(64);
    pub const BALANCED: Self = Self(128);
    pub const STABLE: Self = Self(256);
    pub const HIGH_LATENCY: Self = Self(512);
    
    pub fn as_usize(self) -> usize {
        self.0
    }
}

/// Audio channel configuration.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ChannelConfig {
    Mono,
    Stereo,
    Multi(u32),
}

impl ChannelConfig {
    pub fn count(&self) -> u32 {
        match self {
            ChannelConfig::Mono => 1,
            ChannelConfig::Stereo => 2, 
            ChannelConfig::Multi(n) => *n,
        }
    }
}

/// Parameter value that can be automated.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct ParameterValue {
    pub normalized: f32,  // 0.0 to 1.0
    pub display: f32,     // actual parameter value
}

impl ParameterValue {
    pub fn new(normalized: f32, display: f32) -> Self {
        Self {
            normalized: normalized.clamp(0.0, 1.0),
            display,
        }
    }
}

/// Parameter definition for plugins and built-in processors.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Parameter {
    pub id: String,
    pub name: String,
    pub min_value: f32,
    pub max_value: f32,
    pub default_value: f32,
    pub step_size: Option<f32>,
    pub unit: Option<String>,
    pub is_automatable: bool,
}

/// Audio processing graph node trait.
pub trait AudioNode {
    /// Process audio buffer in real-time.
    fn process(&mut self, input: &[f32], output: &mut [f32]) -> crate::AudioResult<()>;
    
    /// Get parameter value by ID.
    fn get_parameter(&self, id: &str) -> Option<ParameterValue>;
    
    /// Set parameter value by ID.
    fn set_parameter(&mut self, id: &str, value: ParameterValue) -> crate::AudioResult<()>;
    
    /// Get list of parameters.
    fn parameters(&self) -> &[Parameter];
}