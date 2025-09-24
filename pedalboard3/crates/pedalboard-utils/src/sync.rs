//! Synchronization utilities for real-time audio processing.

use std::sync::atomic::{AtomicU32, AtomicBool, Ordering};

/// Atomic parameter value for real-time parameter updates.
#[derive(Debug)]
pub struct AtomicParameter {
    normalized: AtomicU32,  // f32 bits stored as u32
    changed: AtomicBool,
}

impl AtomicParameter {
    /// Create a new atomic parameter with initial value.
    pub fn new(initial_value: f32) -> Self {
        Self {
            normalized: AtomicU32::new(initial_value.to_bits()),
            changed: AtomicBool::new(false),
        }
    }
    
    /// Set parameter value from any thread.
    pub fn set(&self, value: f32) {
        let clamped = value.clamp(0.0, 1.0);
        self.normalized.store(clamped.to_bits(), Ordering::Relaxed);
        self.changed.store(true, Ordering::Relaxed);
    }
    
    /// Get parameter value from audio thread.
    pub fn get(&self) -> f32 {
        let bits = self.normalized.load(Ordering::Relaxed);
        f32::from_bits(bits)
    }
    
    /// Check if parameter changed since last check.
    pub fn check_changed(&self) -> bool {
        self.changed.swap(false, Ordering::Relaxed)
    }
}

/// Real-time safe event for audio thread communication.
#[derive(Debug, Clone, Copy)]
pub enum AudioEvent {
    ParameterChange { parameter_id: u32, value: f32 },
    TransportPlay,
    TransportStop,
    TransportPause,
    PatchChange { patch_id: u32 },
}

/// Lock-free event queue for audio thread communication.
pub struct AudioEventQueue {
    queue: crossbeam::queue::ArrayQueue<AudioEvent>,
}

impl AudioEventQueue {
    /// Create a new event queue with specified capacity.
    pub fn new(capacity: usize) -> Self {
        Self {
            queue: crossbeam::queue::ArrayQueue::new(capacity),
        }
    }
    
    /// Send event to audio thread (non-blocking).
    pub fn send(&self, event: AudioEvent) -> Result<(), AudioEvent> {
        self.queue.push(event)
    }
    
    /// Receive event in audio thread (non-blocking).
    pub fn receive(&self) -> Option<AudioEvent> {
        self.queue.pop()
    }
    
    /// Process all available events with callback.
    pub fn process_events<F>(&self, mut callback: F) 
    where 
        F: FnMut(AudioEvent)
    {
        while let Some(event) = self.receive() {
            callback(event);
        }
    }
}