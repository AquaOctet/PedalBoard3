//! Plugin Manager Panel

use eframe::egui;
use pedalboard_plugins::PluginInfo;
use std::collections::HashMap;

/// Plugin manager panel for loading and configuring plugins
pub struct PluginManagerPanel {
    search_text: String,
    selected_category: String,
    loaded_plugins: Vec<LoadedPlugin>,
    plugin_chain_order: Vec<usize>,
}

#[derive(Debug, Clone)]
struct LoadedPlugin {
    info: PluginInfo,
    enabled: bool,
    bypass: bool,
    parameters: HashMap<String, f32>,
}

impl PluginManagerPanel {
    pub fn new() -> Self {
        Self {
            search_text: String::new(),
            selected_category: "All".to_string(),
            loaded_plugins: Vec::new(),
            plugin_chain_order: Vec::new(),
        }
    }
    
    pub fn render(
        &mut self,
        ctx: &egui::Context,
        open: &mut bool,
        available_plugins: &[PluginInfo],
    ) {
        egui::Window::new("Plugin Manager")
            .open(open)
            .default_size([700.0, 500.0])
            .show(ctx, |ui| {
                // Header with search and filter
                ui.horizontal(|ui| {
                    ui.label("Search:");
                    ui.text_edit_singleline(&mut self.search_text);
                    
                    ui.separator();
                    
                    ui.label("Category:");
                    egui::ComboBox::from_label("")
                        .selected_text(&self.selected_category)
                        .show_ui(ui, |ui| {
                            ui.selectable_value(&mut self.selected_category, "All".to_string(), "All");
                            ui.selectable_value(&mut self.selected_category, "Effect".to_string(), "Effects");
                            ui.selectable_value(&mut self.selected_category, "Instrument".to_string(), "Instruments");
                            ui.selectable_value(&mut self.selected_category, "Utility".to_string(), "Utilities");
                        });
                });
                
                ui.separator();
                
                // Two-column layout
                ui.horizontal(|ui| {
                    // Left column - Available plugins
                    ui.vertical(|ui| {
                        ui.heading("Available Plugins");
                        ui.separator();
                        
                        egui::ScrollArea::vertical()
                            .max_height(300.0)
                            .show(ui, |ui| {
                                if available_plugins.is_empty() {
                                    ui.label("No plugins found. Click 'Scan for Plugins' in the menu.");
                                } else {
                                    for plugin in available_plugins {
                                        if self.matches_filter(plugin) {
                                            self.render_plugin_item(ui, plugin);
                                        }
                                    }
                                }
                            });
                    });
                    
                    ui.separator();
                    
                    // Right column - Loaded plugins chain
                    ui.vertical(|ui| {
                        ui.heading("Plugin Chain");
                        ui.separator();
                        
                        egui::ScrollArea::vertical()
                            .max_height(300.0)
                            .show(ui, |ui| {
                                if self.loaded_plugins.is_empty() {
                                    ui.label("No plugins loaded. Drag plugins from the left panel.");
                                } else {
                                    // Process plugins without borrowing conflicts
                                    let mut plugins_to_remove = Vec::new();
                                    for (i, loaded_plugin) in self.loaded_plugins.iter_mut().enumerate() {
                                        ui.horizontal(|ui| {
                                            // Drag handle
                                            ui.label("☰");
                                            
                                            // Plugin name and controls
                                            ui.vertical(|ui| {
                                                ui.horizontal(|ui| {
                                                    ui.strong(&loaded_plugin.info.name);
                                                    
                                                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                                                        if ui.small_button("❌").clicked() {
                                                            plugins_to_remove.push(i);
                                                        }
                                                        
                                                        if ui.small_button(if loaded_plugin.bypass { "⏵️" } else { "⏸️" }).clicked() {
                                                            loaded_plugin.bypass = !loaded_plugin.bypass;
                                                        }
                                                        
                                                        ui.checkbox(&mut loaded_plugin.enabled, "");
                                                    });
                                                });
                                                
                                                // Plugin parameters (demo)
                                                if ui.small_button("Show Parameters").clicked() {
                                                    tracing::info!("Parameter editor requested for {}", loaded_plugin.info.name);
                                                }
                                                
                                                // Demo parameters
                                                ui.horizontal(|ui| {
                                                    ui.label("Dry/Wet:");
                                                    let mut dry_wet = loaded_plugin.parameters.get("dry_wet").copied().unwrap_or(0.5);
                                                    ui.add(egui::Slider::new(&mut dry_wet, 0.0..=1.0));
                                                    loaded_plugin.parameters.insert("dry_wet".to_string(), dry_wet);
                                                });
                                            });
                                        });
                                        ui.separator();
                                    }
                                    
                                    // Remove plugins marked for removal (in reverse order to preserve indices)
                                    for &index in plugins_to_remove.iter().rev() {
                                        if index < self.loaded_plugins.len() {
                                            self.loaded_plugins.remove(index);
                                        }
                                    }
                                }
                            });
                    });
                });
                
                ui.separator();
                
                // Bottom controls
                ui.horizontal(|ui| {
                    if ui.button("Clear All").clicked() {
                        self.loaded_plugins.clear();
                        self.plugin_chain_order.clear();
                    }
                    
                    ui.separator();
                    
                    if ui.button("Save Chain").clicked() {
                        // In a real implementation, this would save the plugin chain to a file
                        tracing::info!("Plugin chain save requested");
                    }
                    
                    if ui.button("Load Chain").clicked() {
                        // In a real implementation, this would load a plugin chain from a file
                        tracing::info!("Plugin chain load requested");
                    }
                });
            });
    }
    
    fn matches_filter(&self, plugin: &PluginInfo) -> bool {
        // Text search
        if !self.search_text.is_empty() {
            let search_lower = self.search_text.to_lowercase();
            if !plugin.name.to_lowercase().contains(&search_lower)
                && !plugin.vendor.to_lowercase().contains(&search_lower)
            {
                return false;
            }
        }
        
        // Category filter (simplified)
        if self.selected_category != "All" {
            // In a real implementation, you'd have proper category classification
            let category_match = match self.selected_category.as_str() {
                "Effects" => plugin.name.to_lowercase().contains("effect")
                    || plugin.name.to_lowercase().contains("reverb")
                    || plugin.name.to_lowercase().contains("delay")
                    || plugin.name.to_lowercase().contains("chorus")
                    || plugin.name.to_lowercase().contains("distortion"),
                "Instruments" => plugin.name.to_lowercase().contains("synth")
                    || plugin.name.to_lowercase().contains("piano")
                    || plugin.name.to_lowercase().contains("organ")
                    || plugin.name.to_lowercase().contains("drum"),
                "Utilities" => plugin.name.to_lowercase().contains("eq")
                    || plugin.name.to_lowercase().contains("compressor")
                    || plugin.name.to_lowercase().contains("analyzer"),
                _ => true,
            };
            
            if !category_match {
                return false;
            }
        }
        
        true
    }
    
    fn render_plugin_item(&mut self, ui: &mut egui::Ui, plugin: &PluginInfo) {
        ui.horizontal(|ui| {
            // Plugin format icon
            let icon = match plugin.format {
                pedalboard_plugins::PluginFormat::VST2 => "📦",
                pedalboard_plugins::PluginFormat::VST3 => "🎛️",
                pedalboard_plugins::PluginFormat::AU => "🍎",
                pedalboard_plugins::PluginFormat::CLAP => "👏",
                pedalboard_plugins::PluginFormat::LV2 => "🔧",
            };
            
            ui.label(icon);
            
            // Plugin info
            ui.vertical(|ui| {
                ui.strong(&plugin.name);
                ui.small(&plugin.vendor);
            });
            
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button("Add").clicked() {
                    self.add_plugin(plugin.clone());
                }
            });
        });
    }
    
    
    fn add_plugin(&mut self, plugin_info: PluginInfo) {
        let loaded_plugin = LoadedPlugin {
            info: plugin_info.clone(),
            enabled: true,
            bypass: false,
            parameters: HashMap::new(),
        };
        
        self.loaded_plugins.push(loaded_plugin);
        tracing::info!("Added plugin: {} by {}", plugin_info.name, plugin_info.vendor);
    }
}

impl Default for PluginManagerPanel {
    fn default() -> Self {
        Self::new()
    }
}