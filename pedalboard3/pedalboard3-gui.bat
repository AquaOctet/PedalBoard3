@echo off
title Pedalboard3 GUI - Modern Audio Plugin Host
cls

echo.
echo  🎸 Pedalboard3 GUI - Modern Audio Plugin Host
echo ================================================
echo.

REM Check if Rust is installed
where cargo >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ❌ Error: Rust/Cargo is not installed or not in PATH
    echo Please install Rust from: https://rustup.rs/
    echo.
    pause
    exit /b 1
)

echo ✅ Rust/Cargo found

REM Check if we're in the right directory
if not exist "Cargo.toml" (
    echo ❌ Error: Cargo.toml not found
    echo Please run this batch file from the Pedalboard3 project root directory
    echo.
    pause
    exit /b 1
)

echo ✅ Project directory confirmed

REM Build and run the GUI application
echo.
echo 🔨 Building Pedalboard3 GUI...
echo This may take a few minutes on the first run...
echo.

cargo run --bin pedalboard3-gui --features="default"
if %ERRORLEVEL% neq 0 (
    echo.
    echo ❌ Build or execution failed
    echo Check the error messages above for details
    echo.
    pause
    exit /b 1
)

echo.
echo ✅ Pedalboard3 GUI closed successfully
pause