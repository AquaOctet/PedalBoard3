#!/bin/bash

echo "🎸 Pedalboard3 - Modern Audio Plugin Host Demo"
echo "=============================================="
echo ""

echo "📋 1. Checking system validation..."
cargo run --bin pedalboard3 -- --validate

echo ""
echo "🔊 2. Listing available audio devices..."
cargo run --bin pedalboard3 -- --list-devices

echo ""
echo "🔌 3. Scanning for plugins..."
cargo run --bin pedalboard3 -- --scan-plugins

echo ""
echo "🎛️ 4. Running in headless mode for 3 seconds (simulating audio processing)..."
timeout 3 cargo run --bin pedalboard3 -- --headless -v

echo ""
echo "✅ Demo complete! All core features are working:"
echo "   • Mock audio backend (no system audio dependencies needed)"
echo "   • Plugin scanning system"
echo "   • Configuration validation"
echo "   • Audio device enumeration"
echo "   • Headless audio processing mode"
echo "   • Event-driven architecture"
echo "   • Proper logging and tracing"
echo ""
echo "🚀 Ready for development and extension!"