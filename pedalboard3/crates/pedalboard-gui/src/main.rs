//! Pedalboard3 GUI Application Entry Point

use anyhow::Result;
use eframe::egui;
use pedalboard_gui::Pedalboard3App;
use tracing::{error, info};

#[tokio::main]
async fn main() -> Result<()> {
    // Initialize logging
    tracing_subscriber::fmt()
        .with_max_level(tracing::Level::INFO)
        .init();

    info!("Starting Pedalboard3 GUI");

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1200.0, 800.0])
            .with_min_inner_size([800.0, 600.0])
            .with_icon(eframe::icon_data::from_png_bytes(&[]).unwrap_or_default()),
        ..Default::default()
    };

    eframe::run_native(
        "Pedalboard3 - Modern Audio Plugin Host",
        options,
        Box::new(|cc| {
            // Configure egui visuals
            let mut visuals = egui::Visuals::dark();
            visuals.override_text_color = Some(egui::Color32::WHITE);
            cc.egui_ctx.set_visuals(visuals);

            // Create the app
            Box::new(Pedalboard3App::new(cc))
        }),
    )
    .map_err(|e| {
        error!("GUI error: {}", e);
        anyhow::anyhow!("Failed to run GUI: {}", e)
    })
}