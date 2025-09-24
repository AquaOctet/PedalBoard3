//! Main GUI Application

use crate::audio_viz::AudioVisualizer;
use crate::plugin_manager::PluginManagerPanel;
use eframe::egui;
use pedalboard_audio::{DeviceInfo, DeviceManager};
use pedalboard_plugins::{PluginInfo, PluginScanner};
use tracing::{info, warn};

/// Main application state
pub struct Pedalboard3App {
    // Audio system
    device_manager: DeviceManager,
    selected_input_device: Option<String>,
    selected_output_device: Option<String>,
    available_devices: Vec<DeviceInfo>,
    audio_visualizer: AudioVisualizer,
    
    // Plugin system
    plugin_scanner: PluginScanner,
    available_plugins: Vec<PluginInfo>,
    plugin_manager: PluginManagerPanel,
    scanning_plugins: bool,
    
    // UI State
    show_device_panel: bool,
    show_plugin_panel: bool,
    show_mixer_panel: bool,
    show_settings_panel: bool,
    
    // Audio processing
    is_audio_running: bool,
    sample_rate: f32,
    buffer_size: usize,
    cpu_usage: f32,
    
    // Demo mode for visualization
    demo_time: f32,
    demo_frequency: f32,
}

impl Pedalboard3App {
    pub fn new(_cc: &eframe::CreationContext<'_>) -> Self {
        let device_manager = DeviceManager::new();
        let plugin_scanner = PluginScanner::new();
        
        let mut app = Self {
            device_manager,
            selected_input_device: None,
            selected_output_device: None,
            available_devices: Vec::new(),
            audio_visualizer: AudioVisualizer::new(),
            
            plugin_scanner,
            available_plugins: Vec::new(),
            plugin_manager: PluginManagerPanel::new(),
            scanning_plugins: false,
            
            show_device_panel: true,
            show_plugin_panel: true,
            show_mixer_panel: true,
            show_settings_panel: false,
            
            is_audio_running: false,
            sample_rate: 44100.0,
            buffer_size: 512,
            cpu_usage: 0.0,
            
            demo_time: 0.0,
            demo_frequency: 440.0,
        };
        
        // Initialize audio devices
        if let Ok(devices) = app.device_manager.list_devices() {
            app.available_devices = devices;
            info!("Found {} audio devices", app.available_devices.len());
        }
        
        app
    }
    
    fn scan_plugins(&mut self) {
        if self.scanning_plugins {
            return;
        }
        
        self.scanning_plugins = true;
        info!("Starting plugin scan...");
        
        match self.plugin_scanner.scan_all() {
            Ok(plugins) => {
                self.available_plugins = plugins;
                info!("Found {} plugins", self.available_plugins.len());
            }
            Err(e) => {
                warn!("Plugin scan error: {}", e);
            }
        }
        
        self.scanning_plugins = false;
    }
    
    fn render_menu_bar(&mut self, ui: &mut egui::Ui) {
        egui::menu::bar(ui, |ui| {
            ui.menu_button("File", |ui| {
                if ui.button("New Session").clicked() {
                    info!("New session requested");
                }
                if ui.button("Open Session...").clicked() {
                    info!("Open session requested");
                }
                if ui.button("Save Session").clicked() {
                    info!("Save session requested");
                }
                ui.separator();
                if ui.button("Exit").clicked() {
                    std::process::exit(0);
                }
            });
            
            ui.menu_button("Audio", |ui| {
                if ui.button("Start Audio Engine").clicked() {
                    self.is_audio_running = true;
                    info!("Audio engine started");
                }
                if ui.button("Stop Audio Engine").clicked() {
                    self.is_audio_running = false;
                    info!("Audio engine stopped");
                }
                ui.separator();
                if ui.button("Audio Settings...").clicked() {
                    self.show_settings_panel = true;
                }
            });
            
            ui.menu_button("Plugins", |ui| {
                if ui.button("Scan for Plugins").clicked() {
                    self.scan_plugins();
                }
                if ui.button("Plugin Manager").clicked() {
                    self.show_plugin_panel = true;
                }
            });
            
            ui.menu_button("View", |ui| {
                ui.checkbox(&mut self.show_device_panel, "Device Panel");
                ui.checkbox(&mut self.show_plugin_panel, "Plugin Panel");
                ui.checkbox(&mut self.show_mixer_panel, "Mixer Panel");
                ui.checkbox(&mut self.show_settings_panel, "Settings Panel");
            });
            
            ui.menu_button("Help", |ui| {
                if ui.button("About").clicked() {
                    info!("About dialog requested");
                }
            });
        });
    }
    
    fn render_status_bar(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            // Audio status
            let status_color = if self.is_audio_running {
                egui::Color32::GREEN
            } else {
                egui::Color32::RED
            };
            
            ui.colored_label(status_color, if self.is_audio_running { "●" } else { "●" });
            ui.label(if self.is_audio_running { "Audio Running" } else { "Audio Stopped" });
            
            ui.separator();
            
            // Sample rate and buffer size
            ui.label(format!("{}Hz", self.sample_rate as u32));
            ui.label(format!("Buffer: {}", self.buffer_size));
            
            ui.separator();
            
            // CPU usage
            ui.label(format!("CPU: {:.1}%", self.cpu_usage));
            
            ui.separator();
            
            // Plugin count
            ui.label(format!("Plugins: {}", self.available_plugins.len()));
            
            if self.scanning_plugins {
                ui.spinner();
                ui.label("Scanning...");
            }
        });
    }
    
    fn render_device_panel(&mut self, ctx: &egui::Context) {
        egui::Window::new("Audio Devices")
            .open(&mut self.show_device_panel)
            .default_size([400.0, 300.0])
            .show(ctx, |ui| {
                ui.heading("Audio Device Configuration");
                
                ui.separator();
                
                // Input device selection
                ui.label("Input Device:");
                egui::ComboBox::from_label("")
                    .selected_text(self.selected_input_device.as_deref().unwrap_or("Select Input..."))
                    .show_ui(ui, |ui| {
                        for device in &self.available_devices {
                            if device.is_input {
                                let selected = ui.selectable_value(
                                    &mut self.selected_input_device,
                                    Some(device.name.clone()),
                                    &device.name
                                ).clicked();
                                
                                if selected {
                                    info!("Selected input device: {}", device.name);
                                }
                            }
                        }
                    });
                
                ui.add_space(10.0);
                
                // Output device selection
                ui.label("Output Device:");
                egui::ComboBox::from_label(" ")
                    .selected_text(self.selected_output_device.as_deref().unwrap_or("Select Output..."))
                    .show_ui(ui, |ui| {
                        for device in &self.available_devices {
                            if device.is_output {
                                let selected = ui.selectable_value(
                                    &mut self.selected_output_device,
                                    Some(device.name.clone()),
                                    &device.name
                                ).clicked();
                                
                                if selected {
                                    info!("Selected output device: {}", device.name);
                                }
                            }
                        }
                    });
                
                ui.add_space(20.0);
                
                // Device list
                ui.heading("Available Devices");
                egui::ScrollArea::vertical().show(ui, |ui| {
                    for device in &self.available_devices {
                        ui.horizontal(|ui| {
                            let icon = if device.is_input && device.is_output {
                                "🎙️🔊"
                            } else if device.is_input {
                                "🎙️"
                            } else {
                                "🔊"
                            };
                            
                            ui.label(icon);
                            ui.label(&device.name);
                        });
                    }
                });
                
                ui.add_space(10.0);
                
                if ui.button("Refresh Devices").clicked() {
                    match self.device_manager.list_devices() {
                        Ok(devices) => {
                            self.available_devices = devices;
                            info!("Refreshed device list");
                        }
                        Err(e) => {
                            warn!("Failed to refresh devices: {}", e);
                        }
                    }
                }
            });
    }
    
    fn render_mixer_panel(&mut self, ctx: &egui::Context) {
        egui::Window::new("Audio Mixer & Visualization")
            .open(&mut self.show_mixer_panel)
            .default_size([600.0, 400.0])
            .show(ctx, |ui| {
                ui.heading("Audio Mixer");
                
                // Master controls
                ui.horizontal(|ui| {
                    ui.label("Master Volume:");
                    ui.add(egui::Slider::new(&mut self.cpu_usage, 0.0..=100.0).suffix("%"));
                });
                
                ui.separator();
                
                // Audio visualization
                self.audio_visualizer.render(ui, self.demo_time);
                
                ui.separator();
                
                // Demo controls
                ui.heading("Demo Audio Generator");
                ui.horizontal(|ui| {
                    ui.label("Frequency:");
                    ui.add(egui::Slider::new(&mut self.demo_frequency, 220.0..=880.0).suffix(" Hz"));
                });
                
                if ui.button(if self.is_audio_running { "Stop Demo" } else { "Start Demo" }).clicked() {
                    self.is_audio_running = !self.is_audio_running;
                    if self.is_audio_running {
                        info!("Started demo audio at {}Hz", self.demo_frequency);
                    } else {
                        info!("Stopped demo audio");
                    }
                }
            });
    }
    
    fn render_settings_panel(&mut self, ctx: &egui::Context) {
        egui::Window::new("Settings")
            .open(&mut self.show_settings_panel)
            .default_size([400.0, 300.0])
            .show(ctx, |ui| {
                ui.heading("Audio Settings");
                
                ui.horizontal(|ui| {
                    ui.label("Sample Rate:");
                    egui::ComboBox::from_label("")
                        .selected_text(format!("{}Hz", self.sample_rate as u32))
                        .show_ui(ui, |ui| {
                            ui.selectable_value(&mut self.sample_rate, 44100.0, "44.1 kHz");
                            ui.selectable_value(&mut self.sample_rate, 48000.0, "48.0 kHz");
                            ui.selectable_value(&mut self.sample_rate, 88200.0, "88.2 kHz");
                            ui.selectable_value(&mut self.sample_rate, 96000.0, "96.0 kHz");
                        });
                });
                
                ui.horizontal(|ui| {
                    ui.label("Buffer Size:");
                    egui::ComboBox::from_label(" ")
                        .selected_text(format!("{} samples", self.buffer_size))
                        .show_ui(ui, |ui| {
                            ui.selectable_value(&mut self.buffer_size, 64, "64 samples");
                            ui.selectable_value(&mut self.buffer_size, 128, "128 samples");
                            ui.selectable_value(&mut self.buffer_size, 256, "256 samples");
                            ui.selectable_value(&mut self.buffer_size, 512, "512 samples");
                            ui.selectable_value(&mut self.buffer_size, 1024, "1024 samples");
                        });
                });
            });
    }
}

impl eframe::App for Pedalboard3App {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        // Update demo time for animations
        self.demo_time += ctx.input(|i| i.unstable_dt);
        
        // Update CPU usage simulation
        if self.is_audio_running {
            self.cpu_usage = 15.0 + 10.0 * (self.demo_time * 0.5).sin().abs();
        } else {
            self.cpu_usage = 2.0;
        }
        
        // Top menu bar
        egui::TopBottomPanel::top("top_panel").show(ctx, |ui| {
            self.render_menu_bar(ui);
        });
        
        // Bottom status bar
        egui::TopBottomPanel::bottom("status_bar").show(ctx, |ui| {
            self.render_status_bar(ui);
        });
        
        // Central panel with main content
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("🎸 Pedalboard3 - Modern Audio Plugin Host");
            ui.separator();
            
            // Welcome message and quick actions
            if !self.is_audio_running {
                ui.horizontal(|ui| {
                    if ui.button("🔊 Start Audio Engine").clicked() {
                        self.is_audio_running = true;
                        info!("Audio engine started from main panel");
                    }
                    
                    if ui.button("🔌 Scan Plugins").clicked() {
                        self.scan_plugins();
                    }
                    
                    if ui.button("⚙️ Settings").clicked() {
                        self.show_settings_panel = true;
                    }
                });
                
                ui.add_space(20.0);
                
                ui.label("Welcome to Pedalboard3! Select audio devices and start the audio engine to begin.");
            } else {
                ui.horizontal(|ui| {
                    if ui.button("⏹️ Stop Audio Engine").clicked() {
                        self.is_audio_running = false;
                        info!("Audio engine stopped from main panel");
                    }
                    
                    ui.label(format!("Audio running at {}Hz", self.sample_rate as u32));
                });
                
                ui.add_space(10.0);
                
                // Show mini visualizer in main panel
                ui.label("Audio Activity:");
                self.audio_visualizer.render_compact(ui, self.demo_time);
            }
        });
        
        // Render floating panels
        if self.show_device_panel {
            self.render_device_panel(ctx);
        }
        
        if self.show_plugin_panel {
            self.plugin_manager.render(ctx, &mut self.show_plugin_panel, &self.available_plugins);
        }
        
        if self.show_mixer_panel {
            self.render_mixer_panel(ctx);
        }
        
        if self.show_settings_panel {
            self.render_settings_panel(ctx);
        }
        
        // Request continuous repaint for animations
        ctx.request_repaint();
    }
}