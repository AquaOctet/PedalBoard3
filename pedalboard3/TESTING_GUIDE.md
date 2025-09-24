# Pedalboard3 Testing Guide

**Comprehensive Testing Strategy for the Pedalboard2 to Pedalboard3 Modernization Project**

## Overview

This guide provides practical instructions for testing the Pedalboard3 modernization project, ensuring compatibility and validation against the original Pedalboard2 application.

## Quick Start

### Prerequisites

1. **Rust Toolchain**: Install Rust via [rustup.rs](https://rustup.rs/)
2. **Original Pedalboard2**: Ensure the original executables are available
3. **Audio Dependencies**: Platform-specific audio libraries
4. **Test Plugins**: VST/AU/LADSPA plugins for compatibility testing

### Basic Test Execution

```bash
# Run all tests quickly
./scripts/test_runner.sh --quick

# Run only unit tests
./scripts/test_runner.sh --unit-only

# Run complete test suite (including performance tests)
./scripts/test_runner.sh

# Skip baseline tests if original Pedalboard2 unavailable
./scripts/test_runner.sh --no-baseline
```

## Testing Architecture

### 1. Original Pedalboard2 Baseline Testing

**Purpose**: Establish baseline behavior and performance metrics from the original application.

**Location**: `/tests/baseline_testing.rs`

**Key Tests**:
- Original executable discovery and validation
- Basic execution and version detection
- Performance metric establishment
- Baseline patch library creation

**How to Run**:
```bash
# Test original Pedalboard2 discovery
cargo test test_original_pedalboard_discovery

# Test basic execution
cargo test test_original_pedalboard_basic_execution

# Create baseline patch library  
cargo test test_create_baseline_patch_library
```

**Expected Output**:
```
✓ Found Pedalboard2 installation:
  32-bit: /mnt/d/main/DEV/pedalboard/Pedalboard2.exe
  64-bit: /mnt/d/main/DEV/pedalboard/Pedalboard2-64bit.exe

✓ Original Pedalboard2 execution test:
  Exit code: 0
  Execution time: 1.2s
  Output length: 245 bytes
```

### 2. Audio Processing Validation

**Purpose**: Validate that Rust implementation produces identical audio output to original.

**Location**: `/tests/audio_processing.rs`

**Key Tests**:
- Sine wave passthrough accuracy
- Multichannel audio handling
- Buffer boundary conditions
- Level processor validation (when implemented)

**How to Run**:
```bash
# Test audio processing framework
cargo test audio_processing

# Test specific processing components
cargo test test_sine_wave_passthrough
cargo test test_multichannel_audio_handling
```

**Validation Framework**:
```rust
let tolerance = 0.001; // 0.1% tolerance
assert!(audio_buffers_equal(&original_output, &rust_output, tolerance));
```

### 3. Plugin Compatibility Testing

**Purpose**: Ensure VST/AU/LADSPA plugins load and function correctly.

**Location**: `/tests/plugin_compatibility.rs`

**Key Tests**:
- Plugin format detection
- Plugin scanning and discovery
- Loading and validation
- Cross-platform compatibility

**How to Run**:
```bash
# Test plugin scanning
cargo test test_common_vst_plugin_locations

# Test plugin validation
cargo test test_plugin_validation_framework

# Scan installed plugins (requires actual plugins)
cargo test test_scan_installed_plugins --ignored
```

**Plugin Discovery Results**:
```
Checking VST plugin paths:
  C:\Program Files\VSTPlugins - ✓
    Found 15 potential VST plugins
  /usr/lib/vst - ✗
  ~/.vst - ✓
    Found 8 potential VST plugins
```

### 4. MIDI/OSC Mapping Validation

**Purpose**: Validate MIDI CC and OSC message handling and parameter mapping.

**Location**: `/tests/midi_osc_mapping.rs`

**Key Tests**:
- MIDI CC message processing
- OSC message parsing and routing
- Mapping learning functionality
- Automation curve validation
- Timing accuracy

**How to Run**:
```bash
# Test MIDI mapping framework
cargo test test_midi_cc_mapping_framework

# Test OSC message handling
cargo test test_osc_message_parsing

# Test learning mode
cargo test test_mapping_learning_mode

# Test timing accuracy
cargo test test_timing_accuracy
```

**Timing Requirements**:
- Average processing time: <100μs
- Maximum processing time: <1000μs

### 5. Performance Benchmarking

**Purpose**: Ensure performance meets or exceeds original Pedalboard2.

**Location**: `/tests/performance_benchmarks.rs`

**Key Tests**:
- Audio processing latency measurement
- Memory allocation tracking
- CPU usage monitoring
- Plugin loading performance
- Graph processing scalability

**How to Run**:
```bash
# Run performance benchmarks
cargo test performance_benchmarks

# Run Criterion benchmarks
cargo bench

# Test specific performance metrics
cargo test test_audio_processing_latency
cargo test test_memory_allocation_tracking
```

**Performance Targets**:
- Audio latency: <10ms round-trip
- CPU usage: <50% at 44.1kHz/512 samples
- Memory: Zero allocation during processing
- Plugin loading: <100ms per plugin

### 6. Compatibility Validation

**Purpose**: Ensure full compatibility with original Pedalboard2 workflows and file formats.

**Location**: `/tests/compatibility_validation.rs`

**Key Tests**:
- Patch file discovery and analysis
- File format migration validation
- Compatibility matrix tracking
- Behavior documentation

**How to Run**:
```bash
# Test compatibility validation
cargo test compatibility_validation

# Test patch file analysis
cargo test test_patch_file_discovery
cargo test test_patch_format_analysis

# Test compatibility matrix
cargo test test_compatibility_matrix
```

**Compatibility Matrix**:
```
✓ audio_processing: Planned
✓ plugin_loading: Planned  
✓ midi_mapping: Planned
⚠ file_io: Not Implemented
⚠ gui_layout: Not Implemented
```

## Development Testing Workflow

### For Developers

1. **Before Committing**:
   ```bash
   # Run quick tests
   ./scripts/test_runner.sh --quick
   
   # Check formatting and lints
   cargo fmt --check
   cargo clippy
   ```

2. **Before Pull Request**:
   ```bash
   # Run full test suite
   ./scripts/test_runner.sh
   
   # Run benchmarks
   cargo bench
   ```

3. **Performance Regression Checking**:
   ```bash
   # Run benchmarks and compare with baseline
   cargo bench -- --save-baseline main
   cargo bench -- --baseline main
   ```

### Continuous Integration

The project includes automated testing via GitHub Actions:
- **Build and Unit Tests**: All platforms (Linux, Windows, macOS)
- **Integration Tests**: Comprehensive functionality validation
- **Performance Benchmarks**: Automated performance tracking
- **Plugin Compatibility**: Cross-platform plugin testing
- **Security Checks**: Memory safety and security auditing

### Test Data Management

**Test Audio Files**: Store in `/tests/audio_samples/`
```
tests/audio_samples/
├── sine_440hz_1s.wav
├── white_noise_5s.wav
├── frequency_sweep.wav
└── music_sample.wav
```

**Test Patches**: Store in `/tests/test_patches/`
```
tests/test_patches/
├── simple_passthrough.filtergraph
├── reverb_chain.filtergraph
├── complex_routing.filtergraph
└── stress_test.filtergraph
```

**Test Plugins**: Reference plugins for compatibility testing
```
tests/test_plugins/
├── vst/
│   ├── test_reverb.dll
│   └── test_delay.dll
├── au/
│   └── test_effects.component
└── ladspa/
    └── test_plugins.so
```

## Troubleshooting

### Common Issues

**Original Pedalboard2 Not Found**:
```
⚠ Original Pedalboard2 not found: 32-bit executable not found
```
Solution: Ensure executables are at expected paths or use `--no-baseline`

**Audio Dependencies Missing**:
```
error: linking with `cc` failed
```
Solution: Install platform-specific audio libraries:
- Linux: `sudo apt-get install libjack-jackd2-dev libasound2-dev`
- macOS: `brew install jack`
- Windows: Ensure Windows SDK is installed

**Plugin Tests Failing**:
```
✗ Plugin compatibility tests failed
```
Solution: Install test plugins or use `--quick` to skip plugin tests

### Performance Issues

**Tests Running Slowly**:
- Use `--quick` flag to skip time-consuming tests
- Run specific test categories: `cargo test audio_processing`
- Use release mode: `cargo test --release`

**Memory Issues During Testing**:
- Monitor with: `cargo test -- --test-threads=1`
- Use AddressSanitizer: `RUSTFLAGS="-Z sanitizer=address" cargo +nightly test`

## Test Results and Reporting

### Results Location
- Test results: `/test_results/`
- Benchmark results: `/benchmark_results/`
- Coverage reports: `/coverage/`

### Interpreting Results

**Success Indicators**:
- All tests pass with ✓ marks
- Performance metrics within targets
- Zero memory leaks detected
- Plugin compatibility >95%

**Warning Indicators**:
- Tests pass but with warnings (⚠)
- Performance near but within limits
- Some plugins incompatible but acceptable

**Failure Indicators**:
- Any tests marked with ✗
- Performance targets exceeded
- Memory leaks detected
- Critical compatibility issues

## Integration with Development

### IDE Integration

**VS Code**: Use Rust extension with test discovery
```json
{
    "rust-analyzer.cargo.features": ["test"],
    "rust-analyzer.checkOnSave.command": "test"
}
```

**Test Coverage**:
```bash
# Install tarpaulin
cargo install cargo-tarpaulin

# Generate coverage report
cargo tarpaulin --out Html --output-dir coverage/
```

### Git Hooks

**Pre-commit Hook** (`.git/hooks/pre-commit`):
```bash
#!/bin/bash
./scripts/test_runner.sh --quick
exit $?
```

## Next Steps

1. **Implement Core Audio Graph**: Enable full audio processing tests
2. **Add Plugin Loading**: Enable actual plugin compatibility testing  
3. **Implement GUI**: Enable user interface testing
4. **Add Real Audio I/O**: Enable latency measurement testing
5. **Create Migration Tools**: Enable patch conversion testing

## Getting Help

- **Issues**: File on GitHub with test output and environment details
- **Performance**: Include benchmark results and system specifications
- **Compatibility**: Include plugin lists and platform information

---

**Testing is critical for the success of the Pedalboard3 modernization project. This framework ensures we maintain the quality and compatibility that users expect while delivering the benefits of the Rust implementation.**