//! Audio buffer management for efficient processing

use crate::{AudioBuffer, Sample};

/// Ring buffer for audio streaming
pub struct RingBuffer {
    data: Vec<Sample>,
    capacity: usize,
    read_pos: usize,
    write_pos: usize,
    size: usize,
}

impl RingBuffer {
    /// Create a new ring buffer
    pub fn new(capacity: usize) -> Self {
        Self {
            data: vec![0.0; capacity],
            capacity,
            read_pos: 0,
            write_pos: 0,
            size: 0,
        }
    }

    /// Write samples to the buffer
    pub fn write(&mut self, samples: &[Sample]) -> usize {
        let mut written = 0;
        for &sample in samples {
            if self.size < self.capacity {
                self.data[self.write_pos] = sample;
                self.write_pos = (self.write_pos + 1) % self.capacity;
                self.size += 1;
                written += 1;
            } else {
                break;
            }
        }
        written
    }

    /// Read samples from the buffer
    pub fn read(&mut self, output: &mut [Sample]) -> usize {
        let mut read = 0;
        for sample in output.iter_mut() {
            if self.size > 0 {
                *sample = self.data[self.read_pos];
                self.read_pos = (self.read_pos + 1) % self.capacity;
                self.size -= 1;
                read += 1;
            } else {
                *sample = 0.0;
            }
        }
        read
    }

    /// Available space for writing
    pub fn available_write(&self) -> usize {
        self.capacity - self.size
    }

    /// Available data for reading
    pub fn available_read(&self) -> usize {
        self.size
    }

    /// Clear the buffer
    pub fn clear(&mut self) {
        self.read_pos = 0;
        self.write_pos = 0;
        self.size = 0;
    }
}

/// Audio buffer utilities
pub struct BufferUtils;

impl BufferUtils {
    /// Mix two buffers together
    pub fn mix(a: &mut [Sample], b: &[Sample]) {
        for (a_sample, &b_sample) in a.iter_mut().zip(b.iter()) {
            *a_sample += b_sample;
        }
    }

    /// Apply gain to buffer
    pub fn apply_gain(buffer: &mut [Sample], gain: Sample) {
        for sample in buffer.iter_mut() {
            *sample *= gain;
        }
    }

    /// Clear buffer
    pub fn clear(buffer: &mut [Sample]) {
        for sample in buffer.iter_mut() {
            *sample = 0.0;
        }
    }

    /// Copy buffer
    pub fn copy(src: &[Sample], dst: &mut [Sample]) {
        let len = src.len().min(dst.len());
        dst[..len].copy_from_slice(&src[..len]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ring_buffer_basic() {
        let mut buffer = RingBuffer::new(4);
        
        // Write some data
        let input = [1.0, 2.0, 3.0];
        let written = buffer.write(&input);
        assert_eq!(written, 3);
        
        // Read it back
        let mut output = [0.0; 3];
        let read = buffer.read(&mut output);
        assert_eq!(read, 3);
        assert_eq!(output, [1.0, 2.0, 3.0]);
    }

    #[test]
    fn test_buffer_utils_mix() {
        let mut a = [1.0, 2.0, 3.0];
        let b = [0.5, 1.0, 1.5];
        BufferUtils::mix(&mut a, &b);
        assert_eq!(a, [1.5, 3.0, 4.5]);
    }

    #[test]
    fn test_buffer_utils_gain() {
        let mut buffer = [1.0, 2.0, 3.0];
        BufferUtils::apply_gain(&mut buffer, 0.5);
        assert_eq!(buffer, [0.5, 1.0, 1.5]);
    }
}