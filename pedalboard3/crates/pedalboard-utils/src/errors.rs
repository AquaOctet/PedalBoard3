//! Common error types and utilities for Pedalboard3.

use thiserror::Error;

/// Core error type for Pedalboard3 operations.
#[derive(Error, Debug, Clone)]
pub enum PedalboardError {
    /// Audio system errors
    #[error("Audio error: {message}")]
    Audio { message: String },
    
    /// Plugin-related errors  
    #[error("Plugin error: {plugin_name} - {message}")]
    Plugin { plugin_name: String, message: String },
    
    /// MIDI/OSC communication errors
    #[error("Control error: {message}")]
    Control { message: String },
    
    /// Configuration and file I/O errors
    #[error("Config error: {message}")]
    Config { message: String },
    
    /// GUI and user interface errors
    #[error("UI error: {message}")]
    Ui { message: String },
    
    /// General system errors
    #[error("System error: {message}")]
    System { message: String },
}

/// Result type alias for Pedalboard operations.
pub type PedalboardResult<T> = Result<T, PedalboardError>;

/// Audio-specific errors that must be stack-allocated for real-time safety.
#[derive(Error, Debug, Copy, Clone)]
pub enum AudioError {
    /// Buffer underrun or overrun
    #[error("Buffer xrun detected")]
    BufferXrun,
    
    /// Sample rate mismatch
    #[error("Sample rate mismatch: expected {expected}, got {actual}")]
    SampleRateMismatch { expected: u32, actual: u32 },
    
    /// Channel count mismatch
    #[error("Channel count mismatch: expected {expected}, got {actual}")]
    ChannelCountMismatch { expected: u32, actual: u32 },
    
    /// Plugin processing error
    #[error("Plugin processing failed")]
    PluginProcessingFailed,
    
    /// Real-time constraint violation
    #[error("Real-time constraint violation")]
    RealTimeViolation,
}

/// Result type for audio operations that must not allocate.
pub type AudioResult<T> = Result<T, AudioError>;