//! Audio device management and enumeration

use anyhow::Result;
#[cfg(feature = "cpal")]
use cpal::traits::{DeviceTrait, HostTrait};
#[cfg(feature = "cpal")]
use cpal::{Device, Host, SupportedStreamConfig};
use std::fmt;

/// Audio device information
#[derive(Debug, Clone)]
pub struct DeviceInfo {
    pub name: String,
    pub is_input: bool,
    pub is_output: bool,
    #[cfg(feature = "cpal")]
    pub default_config: Option<SupportedStreamConfig>,
    #[cfg(feature = "cpal")]
    pub supported_configs: Vec<SupportedStreamConfig>,
    #[cfg(not(feature = "cpal"))]
    pub sample_rate: u32,
    #[cfg(not(feature = "cpal"))]
    pub channels: u32,
}

impl fmt::Display for DeviceInfo {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        #[cfg(feature = "cpal")]
        {
            write!(f, "{} ({}{})", 
                self.name,
                if self.is_input { "I" } else { "" },
                if self.is_output { "O" } else { "" }
            )
        }
        
        #[cfg(not(feature = "cpal"))]
        {
            write!(f, "{} ({}{}) - {}Hz, {} channels", 
                self.name,
                if self.is_input { "I" } else { "" },
                if self.is_output { "O" } else { "" },
                self.sample_rate,
                self.channels
            )
        }
    }
}

/// Device manager for audio I/O
pub struct DeviceManager {
    #[cfg(feature = "cpal")]
    host: Host,
}

impl DeviceManager {
    /// Create a new device manager
    pub fn new() -> Self {
        #[cfg(feature = "cpal")]
        {
            Self {
                host: cpal::default_host(),
            }
        }
        
        #[cfg(not(feature = "cpal"))]
        {
            Self {}
        }
    }

    /// List all available audio devices
    pub fn list_devices(&self) -> Result<Vec<DeviceInfo>> {
        let mut devices = Vec::new();

        #[cfg(feature = "cpal")]
        {
            // Input devices
            if let Ok(input_devices) = self.host.input_devices() {
                for device in input_devices {
                    if let Ok(device_info) = self.device_info(&device, true) {
                        devices.push(device_info);
                    }
                }
            }

            // Output devices
            if let Ok(output_devices) = self.host.output_devices() {
                for device in output_devices {
                    if let Ok(device_info) = self.device_info(&device, false) {
                        devices.push(device_info);
                    }
                }
            }
        }
        
        #[cfg(not(feature = "cpal"))]
        {
            // Create mock devices
            devices.push(DeviceInfo {
                name: "Mock Input Device".to_string(),
                is_input: true,
                is_output: false,
                sample_rate: 44100,
                channels: 2,
            });
            
            devices.push(DeviceInfo {
                name: "Mock Output Device".to_string(),
                is_input: false,
                is_output: true,
                sample_rate: 44100,
                channels: 2,
            });
            
            devices.push(DeviceInfo {
                name: "Mock Audio Interface".to_string(),
                is_input: true,
                is_output: true,
                sample_rate: 48000,
                channels: 8,
            });
        }

        Ok(devices)
    }

    /// Get device information
    #[cfg(feature = "cpal")]
    fn device_info(&self, device: &Device, is_input: bool) -> Result<DeviceInfo> {
        let name = device.name().unwrap_or_else(|_| "Unknown Device".to_string());
        
        let default_config = if is_input {
            device.default_input_config().ok()
        } else {
            device.default_output_config().ok()
        };

        let supported_configs = if is_input {
            device.supported_input_configs()
                .map(|configs| configs.collect())
                .unwrap_or_default()
        } else {
            device.supported_output_configs()
                .map(|configs| configs.collect())
                .unwrap_or_default()
        };

        Ok(DeviceInfo {
            name,
            is_input,
            is_output: !is_input,
            default_config,
            supported_configs,
        })
    }

    /// Get default input device
    #[cfg(feature = "cpal")]
    pub fn default_input_device(&self) -> Option<Device> {
        self.host.default_input_device()
    }
    
    #[cfg(not(feature = "cpal"))]
    pub fn default_input_device(&self) -> Option<()> {
        Some(()) // Mock device
    }

    /// Get default output device
    #[cfg(feature = "cpal")]
    pub fn default_output_device(&self) -> Option<Device> {
        self.host.default_output_device()
    }
    
    #[cfg(not(feature = "cpal"))]
    pub fn default_output_device(&self) -> Option<()> {
        Some(()) // Mock device
    }

    /// Find device by name
    #[cfg(feature = "cpal")]
    pub fn find_device_by_name(&self, name: &str, is_input: bool) -> Option<Device> {
        let devices = if is_input {
            self.host.input_devices().ok()?
        } else {
            self.host.output_devices().ok()?
        };

        for device in devices {
            if let Ok(device_name) = device.name() {
                if device_name.contains(name) {
                    return Some(device);
                }
            }
        }
        None
    }
    
    #[cfg(not(feature = "cpal"))]
    pub fn find_device_by_name(&self, name: &str, _is_input: bool) -> Option<()> {
        if name.to_lowercase().contains("mock") {
            Some(()) // Mock device
        } else {
            None
        }
    }

    /// Get host information
    pub fn host_name(&self) -> String {
        #[cfg(feature = "cpal")]
        {
            self.host.id().name().to_string()
        }
        
        #[cfg(not(feature = "cpal"))]
        {
            "Mock Audio Host".to_string()
        }
    }
}

impl Default for DeviceManager {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_device_manager_creation() {
        let manager = DeviceManager::new();
        assert!(!manager.host_name().is_empty());
    }

    #[test]
    fn test_list_devices() {
        let manager = DeviceManager::new();
        let result = manager.list_devices();
        assert!(result.is_ok());
    }

    #[test]
    fn test_default_devices() {
        let manager = DeviceManager::new();
        // These might be None on headless systems, but shouldn't panic
        let _input = manager.default_input_device();
        let _output = manager.default_output_device();
    }
}