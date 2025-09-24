//! Audio Visualization Components

use eframe::egui;
use std::collections::VecDeque;

/// Audio visualizer with spectrum and waveform display
pub struct AudioVisualizer {
    waveform_data: VecDeque<f32>,
    spectrum_data: Vec<f32>,
    max_samples: usize,
}

impl AudioVisualizer {
    pub fn new() -> Self {
        let mut spectrum_data = Vec::with_capacity(64);
        spectrum_data.resize(64, 0.0);
        
        Self {
            waveform_data: VecDeque::with_capacity(512),
            spectrum_data,
            max_samples: 512,
        }
    }
    
    /// Generate demo audio data for visualization
    fn generate_demo_data(&mut self, time: f32, frequency: f32) {
        // Generate sine wave with some harmonics
        let sample = (time * frequency * 2.0 * std::f32::consts::PI).sin() * 0.5
                    + (time * frequency * 4.0 * std::f32::consts::PI).sin() * 0.2
                    + (time * frequency * 6.0 * std::f32::consts::PI).sin() * 0.1;
        
        // Add to waveform buffer
        self.waveform_data.push_back(sample);
        if self.waveform_data.len() > self.max_samples {
            self.waveform_data.pop_front();
        }
        
        // Generate spectrum data (simulated FFT)
        let spectrum_len = self.spectrum_data.len();
        for (i, value) in self.spectrum_data.iter_mut().enumerate() {
            let freq_ratio = i as f32 / spectrum_len as f32;
            let base_amplitude = if freq_ratio < 0.1 {
                0.8 * (1.0 - freq_ratio * 10.0)
            } else if freq_ratio < 0.3 {
                0.3 * (1.0 - (freq_ratio - 0.1) * 5.0)
            } else {
                0.1 * (1.0 - freq_ratio)
            };
            
            // Add some randomness and time-based variation
            let variation = (time * (i + 1) as f32 * 0.1).sin() * 0.1;
            *value = (base_amplitude + variation).max(0.0).min(1.0);
        }
    }
    
    pub fn render(&mut self, ui: &mut egui::Ui, time: f32) {
        // Generate demo data
        self.generate_demo_data(time, 440.0);
        
        ui.heading("Audio Visualization");
        
        // Waveform display
        ui.label("Waveform:");
        let waveform_height = 80.0;
        let (rect, _response) = ui.allocate_exact_size(
            egui::Vec2::new(ui.available_width(), waveform_height),
            egui::Sense::hover()
        );
        
        if ui.is_rect_visible(rect) {
            let painter = ui.painter();
            
            // Background
            painter.rect_filled(rect, 2.0, egui::Color32::from_gray(20));
            
            // Zero line
            let zero_y = rect.center().y;
            painter.line_segment(
                [egui::Pos2::new(rect.left(), zero_y), egui::Pos2::new(rect.right(), zero_y)],
                egui::Stroke::new(1.0, egui::Color32::from_gray(60))
            );
            
            // Waveform
            if self.waveform_data.len() > 1 {
                let points: Vec<egui::Pos2> = self.waveform_data
                    .iter()
                    .enumerate()
                    .map(|(i, &sample)| {
                        let x = rect.left() + (i as f32 / self.waveform_data.len() as f32) * rect.width();
                        let y = zero_y - sample * (waveform_height * 0.4);
                        egui::Pos2::new(x, y)
                    })
                    .collect();
                
                for window in points.windows(2) {
                    painter.line_segment(
                        [window[0], window[1]],
                        egui::Stroke::new(2.0, egui::Color32::from_rgb(0, 255, 100))
                    );
                }
            }
        }
        
        ui.add_space(10.0);
        
        // Spectrum analyzer
        ui.label("Spectrum Analyzer:");
        let spectrum_height = 120.0;
        let (rect, _response) = ui.allocate_exact_size(
            egui::Vec2::new(ui.available_width(), spectrum_height),
            egui::Sense::hover()
        );
        
        if ui.is_rect_visible(rect) {
            let painter = ui.painter();
            
            // Background
            painter.rect_filled(rect, 2.0, egui::Color32::from_gray(20));
            
            // Frequency bars
            let bar_width = rect.width() / self.spectrum_data.len() as f32;
            for (i, &amplitude) in self.spectrum_data.iter().enumerate() {
                let x = rect.left() + i as f32 * bar_width;
                let bar_height = amplitude * spectrum_height * 0.9;
                let y = rect.bottom() - bar_height;
                
                let color = if amplitude > 0.8 {
                    egui::Color32::from_rgb(255, 100, 100) // Red for peaks
                } else if amplitude > 0.5 {
                    egui::Color32::from_rgb(255, 255, 100) // Yellow for mid
                } else {
                    egui::Color32::from_rgb(100, 255, 100) // Green for low
                };
                
                painter.rect_filled(
                    egui::Rect::from_min_size(
                        egui::Pos2::new(x, y),
                        egui::Vec2::new(bar_width - 1.0, bar_height)
                    ),
                    1.0,
                    color
                );
            }
            
            // Frequency labels
            painter.text(
                egui::Pos2::new(rect.left() + 5.0, rect.bottom() - 15.0),
                egui::Align2::LEFT_BOTTOM,
                "20Hz",
                egui::FontId::proportional(10.0),
                egui::Color32::WHITE
            );
            
            painter.text(
                egui::Pos2::new(rect.right() - 5.0, rect.bottom() - 15.0),
                egui::Align2::RIGHT_BOTTOM,
                "20kHz",
                egui::FontId::proportional(10.0),
                egui::Color32::WHITE
            );
        }
        
        // VU Meters
        ui.add_space(10.0);
        ui.label("VU Meters:");
        ui.horizontal(|ui| {
            // Left channel
            ui.label("L");
            let level_l = self.waveform_data.back().unwrap_or(&0.0).abs();
            self.render_vu_meter(ui, level_l);
            
            ui.add_space(10.0);
            
            // Right channel
            ui.label("R");
            let level_r = level_l * 0.9; // Simulate slight difference
            self.render_vu_meter(ui, level_r);
        });
    }
    
    pub fn render_compact(&mut self, ui: &mut egui::Ui, time: f32) {
        // Generate demo data
        self.generate_demo_data(time, 440.0);
        
        let compact_height = 40.0;
        let (rect, _response) = ui.allocate_exact_size(
            egui::Vec2::new(ui.available_width(), compact_height),
            egui::Sense::hover()
        );
        
        if ui.is_rect_visible(rect) {
            let painter = ui.painter();
            
            // Background
            painter.rect_filled(rect, 2.0, egui::Color32::from_gray(30));
            
            // Mini spectrum bars
            let bar_width = rect.width() / self.spectrum_data.len() as f32;
            for (i, &amplitude) in self.spectrum_data.iter().enumerate() {
                let x = rect.left() + i as f32 * bar_width;
                let bar_height = amplitude * compact_height * 0.8;
                let y = rect.bottom() - bar_height;
                
                let color = egui::Color32::from_rgb(
                    (100.0 + amplitude * 155.0) as u8,
                    (255.0 * amplitude) as u8,
                    100
                );
                
                painter.rect_filled(
                    egui::Rect::from_min_size(
                        egui::Pos2::new(x, y),
                        egui::Vec2::new(bar_width - 0.5, bar_height)
                    ),
                    0.5,
                    color
                );
            }
        }
    }
    
    fn render_vu_meter(&self, ui: &mut egui::Ui, level: f32) {
        let meter_height = 100.0;
        let meter_width = 20.0;
        
        let (rect, _response) = ui.allocate_exact_size(
            egui::Vec2::new(meter_width, meter_height),
            egui::Sense::hover()
        );
        
        if ui.is_rect_visible(rect) {
            let painter = ui.painter();
            
            // Background
            painter.rect_filled(rect, 2.0, egui::Color32::from_gray(20));
            
            // Level indicator
            let level_height = level * meter_height;
            let level_rect = egui::Rect::from_min_size(
                egui::Pos2::new(rect.left(), rect.bottom() - level_height),
                egui::Vec2::new(meter_width, level_height)
            );
            
            let color = if level > 0.8 {
                egui::Color32::RED
            } else if level > 0.6 {
                egui::Color32::YELLOW
            } else {
                egui::Color32::GREEN
            };
            
            painter.rect_filled(level_rect, 2.0, color);
            
            // Scale marks
            for i in 0..10 {
                let y = rect.bottom() - (i as f32 / 10.0) * meter_height;
                painter.line_segment(
                    [egui::Pos2::new(rect.left(), y), egui::Pos2::new(rect.left() + 5.0, y)],
                    egui::Stroke::new(1.0, egui::Color32::WHITE)
                );
            }
        }
    }
}

impl Default for AudioVisualizer {
    fn default() -> Self {
        Self::new()
    }
}