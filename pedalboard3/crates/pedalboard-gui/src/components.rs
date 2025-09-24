//! Reusable UI Components

use eframe::egui;

/// A knob control widget
pub struct Knob {
    value: f32,
    min: f32,
    max: f32,
    label: String,
}

impl Knob {
    pub fn new(label: impl Into<String>, value: f32, min: f32, max: f32) -> Self {
        Self {
            value,
            min,
            max,
            label: label.into(),
        }
    }
    
    pub fn show(mut self, ui: &mut egui::Ui) -> egui::Response {
        let desired_size = egui::Vec2::splat(60.0);
        let (rect, response) = ui.allocate_exact_size(desired_size, egui::Sense::drag());
        
        if ui.is_rect_visible(rect) {
            let visuals = ui.style().interact(&response);
            let painter = ui.painter();
            
            // Background circle
            painter.circle_filled(rect.center(), 25.0, egui::Color32::from_gray(40));
            painter.circle_stroke(rect.center(), 25.0, egui::Stroke::new(2.0, visuals.fg_stroke.color));
            
            // Value indicator
            let normalized = (self.value - self.min) / (self.max - self.min);
            let angle = -std::f32::consts::PI * 0.75 + normalized * std::f32::consts::PI * 1.5;
            
            let indicator_start = rect.center();
            let indicator_end = indicator_start + egui::Vec2::angled(angle) * 20.0;
            
            painter.line_segment(
                [indicator_start, indicator_end],
                egui::Stroke::new(3.0, egui::Color32::WHITE)
            );
            
            // Label
            painter.text(
                egui::Pos2::new(rect.center().x, rect.bottom() + 10.0),
                egui::Align2::CENTER_TOP,
                &self.label,
                egui::FontId::proportional(12.0),
                visuals.text_color()
            );
            
            // Handle drag
            if response.dragged() {
                let delta = response.drag_delta();
                let sensitivity = (self.max - self.min) * 0.01;
                self.value = (self.value - delta.y * sensitivity).clamp(self.min, self.max);
            }
        }
        
        response
    }
}

/// LED indicator widget
pub struct Led {
    on: bool,
    color: egui::Color32,
    label: String,
}

impl Led {
    pub fn new(label: impl Into<String>, on: bool) -> Self {
        Self {
            on,
            color: egui::Color32::GREEN,
            label: label.into(),
        }
    }
    
    pub fn with_color(mut self, color: egui::Color32) -> Self {
        self.color = color;
        self
    }
    
    pub fn show(self, ui: &mut egui::Ui) -> egui::Response {
        ui.horizontal(|ui| {
            let (rect, response) = ui.allocate_exact_size(egui::Vec2::splat(12.0), egui::Sense::hover());
            
            if ui.is_rect_visible(rect) {
                let painter = ui.painter();
                
                let color = if self.on {
                    self.color
                } else {
                    egui::Color32::from_gray(30)
                };
                
                painter.circle_filled(rect.center(), 6.0, color);
                painter.circle_stroke(rect.center(), 6.0, egui::Stroke::new(1.0, egui::Color32::WHITE));
            }
            
            ui.label(&self.label);
            response
        }).inner
    }
}

/// Fader/Slider widget styled like hardware
pub struct Fader {
    value: f32,
    min: f32,
    max: f32,
    label: String,
    vertical: bool,
}

impl Fader {
    pub fn new(label: impl Into<String>, value: f32, min: f32, max: f32) -> Self {
        Self {
            value,
            min,
            max,
            label: label.into(),
            vertical: true,
        }
    }
    
    pub fn horizontal(mut self) -> Self {
        self.vertical = false;
        self
    }
    
    pub fn show(mut self, ui: &mut egui::Ui) -> egui::Response {
        ui.vertical(|ui| {
            ui.label(&self.label);
            
            let size = if self.vertical {
                egui::Vec2::new(20.0, 100.0)
            } else {
                egui::Vec2::new(100.0, 20.0)
            };
            
            let (rect, response) = ui.allocate_exact_size(size, egui::Sense::drag());
            
            if ui.is_rect_visible(rect) {
                let painter = ui.painter();
                
                // Track
                let track_color = egui::Color32::from_gray(60);
                if self.vertical {
                    painter.rect_filled(
                        egui::Rect::from_center_size(rect.center(), egui::Vec2::new(6.0, rect.height())),
                        3.0,
                        track_color
                    );
                } else {
                    painter.rect_filled(
                        egui::Rect::from_center_size(rect.center(), egui::Vec2::new(rect.width(), 6.0)),
                        3.0,
                        track_color
                    );
                }
                
                // Handle
                let normalized = (self.value - self.min) / (self.max - self.min);
                let handle_pos = if self.vertical {
                    egui::Pos2::new(rect.center().x, rect.bottom() - normalized * rect.height())
                } else {
                    egui::Pos2::new(rect.left() + normalized * rect.width(), rect.center().y)
                };
                
                painter.rect_filled(
                    egui::Rect::from_center_size(handle_pos, egui::Vec2::splat(12.0)),
                    2.0,
                    egui::Color32::WHITE
                );
                
                // Handle drag
                if response.dragged() {
                    let drag_delta = response.drag_delta();
                    let delta = if self.vertical { -drag_delta.y } else { drag_delta.x };
                    let range = self.max - self.min;
                    let track_length = if self.vertical { rect.height() } else { rect.width() };
                    let sensitivity = range / track_length;
                    self.value = (self.value + delta * sensitivity).clamp(self.min, self.max);
                }
            }
            
            response
        }).inner
    }
}

/// Level meter widget
pub fn level_meter(ui: &mut egui::Ui, level: f32, label: &str) -> egui::Response {
    ui.vertical(|ui| {
        ui.label(label);
        
        let height = 80.0;
        let width = 16.0;
        let (rect, response) = ui.allocate_exact_size(egui::Vec2::new(width, height), egui::Sense::hover());
        
        if ui.is_rect_visible(rect) {
            let painter = ui.painter();
            
            // Background
            painter.rect_filled(rect, 2.0, egui::Color32::from_gray(20));
            
            // Level segments
            let segment_height = height / 10.0;
            for i in 0..10 {
                let segment_y = rect.bottom() - (i + 1) as f32 * segment_height;
                let segment_rect = egui::Rect::from_min_size(
                    egui::Pos2::new(rect.left() + 1.0, segment_y + 1.0),
                    egui::Vec2::new(width - 2.0, segment_height - 2.0)
                );
                
                let threshold = i as f32 / 10.0;
                if level > threshold {
                    let color = if i >= 8 {
                        egui::Color32::RED
                    } else if i >= 6 {
                        egui::Color32::YELLOW
                    } else {
                        egui::Color32::GREEN
                    };
                    
                    painter.rect_filled(segment_rect, 1.0, color);
                } else {
                    painter.rect_filled(segment_rect, 1.0, egui::Color32::from_gray(30));
                }
            }
            
            // Border
            painter.rect_stroke(rect, 2.0, egui::Stroke::new(1.0, egui::Color32::WHITE));
        }
        
        response
    }).inner
}