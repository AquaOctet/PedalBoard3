@echo off
title Pedalboard3 GUI Demo - Full Feature Showcase
color 0A
cls

echo.
echo  🎸 ================================================= 🎸
echo     PEDALBOARD3 GUI DEMO - FULL FEATURE SHOWCASE
echo  🎸 ================================================= 🎸
echo.
echo  Modern Audio Plugin Host with Real-Time Visualization
echo  Built with Rust + egui for Maximum Performance
echo.

REM Check system requirements
echo 🔍 Checking System Requirements...
echo.

REM Check if Rust is installed
where cargo >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ❌ Rust/Cargo not found
    echo    Please install Rust from: https://rustup.rs/
    echo    This demo requires Rust to compile and run the GUI
    echo.
    goto :error_exit
)
echo ✅ Rust/Cargo found

REM Check project directory
if not exist "Cargo.toml" (
    echo ❌ Project files not found
    echo    Please run this demo from the Pedalboard3 root directory
    echo.
    goto :error_exit
)
echo ✅ Project files found

REM Check for GUI crate
if not exist "crates\pedalboard-gui" (
    echo ❌ GUI crate not found
    echo    The GUI components appear to be missing
    echo.
    goto :error_exit
)
echo ✅ GUI components found

echo.
echo 🚀 DEMO FEATURES OVERVIEW:
echo ========================
echo.
echo 🎛️  Audio Device Management
echo    • Visual device selection interface
echo    • Real-time device status monitoring
echo    • Mock devices for testing without hardware
echo.
echo 🔌 Plugin Management System
echo    • Automatic plugin scanning (VST2, VST3, AU, CLAP, LV2)
echo    • Drag-and-drop plugin chain creation
echo    • Real-time parameter controls with hardware-style UI
echo.
echo 📊 Real-Time Audio Visualization
echo    • Live waveform display
echo    • Spectrum analyzer with color-coded frequency bands
echo    • Professional VU meters
echo    • CPU usage monitoring
echo.
echo 🎨 Professional Interface
echo    • Dark studio theme
echo    • Hardware-inspired knobs, faders, and LEDs
echo    • Resizable panels and windows
echo    • Keyboard shortcuts for workflow efficiency
echo.
echo 🎵 Built-in Demo Mode
echo    • Works without audio hardware
echo    • Simulated audio devices
echo    • Demo tone generator with real-time visualization
echo    • Plugin system demonstration
echo.

echo Press any key to continue with the demo...
pause >nul
cls

echo.
echo 🔨 Building Pedalboard3 GUI...
echo ===============================
echo.
echo This will compile the complete GUI application.
echo First-time compilation may take 5-10 minutes.
echo Subsequent runs will be much faster!
echo.
echo Building in release mode for optimal performance...
echo.

REM Build the GUI
cargo build --bin pedalboard3-gui --release
if %ERRORLEVEL% neq 0 (
    echo.
    echo ❌ Build Failed
    echo ================
    echo.
    echo The GUI compilation encountered errors.
    echo Common solutions:
    echo  1. Update Rust: rustup update
    echo  2. Clear cache: cargo clean
    echo  3. Check dependencies: cargo check
    echo.
    goto :error_exit
)

echo.
echo ✅ Build Successful!
echo ==================
echo.
echo The Pedalboard3 GUI has been compiled successfully.
echo The executable is ready to run.
echo.

echo 🎯 QUICK START GUIDE:
echo ===================
echo.
echo 1. 🔊 Click "Start Audio Engine" to begin audio processing
echo 2. ⚙️  Open "Audio Devices" panel to select your audio interface
echo 3. 🔌 Use "Plugins → Scan for Plugins" to find installed plugins
echo 4. 📊 View real-time audio visualization in the Mixer panel
echo 5. 🎛️  Add plugins to create your processing chain
echo.
echo The demo includes:
echo • Mock audio devices (no hardware required)
echo • Built-in tone generator for testing
echo • Sample plugin chain demonstration
echo • All GUI features fully functional
echo.

echo Press any key to launch the GUI demo...
pause >nul

echo.
echo 🎸 Launching Pedalboard3 GUI...
echo ==============================
echo.

REM Run the GUI
target\release\pedalboard3-gui.exe
if %ERRORLEVEL% neq 0 (
    echo.
    echo ⚠️  GUI Exit Code: %ERRORLEVEL%
    echo.
    if %ERRORLEVEL% equ 1 (
        echo This might be normal if you closed the application.
    ) else (
        echo The application may have encountered an issue.
        echo Check the console output above for any error messages.
    )
    echo.
) else (
    echo.
    echo ✅ GUI Demo Completed Successfully!
    echo ================================
    echo.
    echo Thank you for trying the Pedalboard3 GUI demo!
    echo.
    echo Key features demonstrated:
    echo • Professional audio plugin host interface
    echo • Real-time audio visualization
    echo • Modern Rust + egui performance
    echo • Cross-platform compatibility
    echo • Hardware-style control interface
    echo.
)

echo 📚 NEXT STEPS:
echo ============
echo.
echo To use with real audio hardware:
echo 1. Install ASIO drivers for low-latency audio
echo 2. Connect your audio interface
echo 3. Install VST plugins in standard directories
echo 4. Configure audio settings in the Settings panel
echo.
echo For development:
echo 1. See GUI_README.md for detailed documentation
echo 2. Check crates\pedalboard-gui\src\ for source code
echo 3. Run 'cargo doc --open' for API documentation
echo.

echo Press any key to exit...
pause >nul
goto :end

:error_exit
echo.
echo ❌ Demo cannot continue due to the above errors.
echo.
echo TROUBLESHOOTING:
echo ===============
echo.
echo 1. Install Rust: https://rustup.rs/
echo 2. Verify you're in the correct directory
echo 3. Check that all project files are present
echo 4. Try running: cargo clean && cargo build
echo.
echo For support, see the README.md file or project documentation.
echo.
pause
exit /b 1

:end
echo.
echo 🎸 Thank you for trying Pedalboard3! 🎸
echo.
exit /b 0