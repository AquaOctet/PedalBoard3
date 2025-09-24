# Pedalboard3 Development Standards

## Project Overview

**Mission:** Modernize Pedalboard2 from C++/JUCE to Rust while preserving all functionality and improving performance, maintainability, and cross-platform support.

**Core Requirements:**
- Maintain sub-10ms audio latency for real-time performance
- Preserve all existing user workflows and file compatibility  
- Implement memory-safe audio processing
- Support VST/AU/CLAP plugin hosting with sandboxing
- Real-time MIDI/OSC parameter mapping
- Modern, responsive GUI with cross-platform support

## Code Standards

### Rust Edition & Tooling
- **Edition:** 2021
- **MSRV:** 1.70.0
- **Formatter:** `cargo fmt` with default rustfmt settings
- **Linter:** `cargo clippy` with pedantic lints enabled
- **Documentation:** All public APIs must have doc comments

### Naming Conventions
- **Crates:** `pedalboard-{domain}` (kebab-case)
- **Modules:** `snake_case`
- **Types:** `PascalCase`  
- **Functions/Variables:** `snake_case`
- **Constants:** `SCREAMING_SNAKE_CASE`
- **Feature flags:** `kebab-case`

### Error Handling
- **Library crates:** Use `thiserror` for structured errors
- **Application crates:** Use `anyhow` for error chaining
- **Audio thread:** NO heap allocations, pre-allocate error types
- **Real-time code:** Use `Result<T, AudioError>` with stack-allocated errors

### Performance Guidelines
- **Audio thread:** No heap allocations, no locks, no system calls
- **Lock-free:** Use atomics and lock-free data structures for real-time communication
- **SIMD:** Utilize SIMD when beneficial for audio processing
- **Profiling:** Benchmark critical paths with `criterion`

## Architecture Standards

### Crate Organization
```
pedalboard3/
├── crates/
│   ├── pedalboard-core/      # Application entry, coordination
│   ├── pedalboard-audio/     # Real-time audio engine  
│   ├── pedalboard-plugins/   # Plugin hosting & management
│   ├── pedalboard-midi/      # MIDI/OSC input handling
│   ├── pedalboard-gui/       # User interface
│   └── pedalboard-utils/     # Shared utilities & traits
```

### Module Structure (per crate)
```rust
src/
├── lib.rs              # Public API and re-exports
├── types.rs            # Core types and traits
├── engine/             # Core implementation
├── config/             # Configuration management
├── tests/              # Unit tests
└── examples/           # Usage examples
```

### Real-Time Safety
- **Audio thread isolation:** Separate thread for audio processing
- **Lock-free communication:** Use `crossbeam::channel` for non-real-time data
- **Pre-allocation:** All audio buffers allocated at startup
- **No panics:** Audio thread must never panic

### Plugin Architecture
- **Sandboxing:** Isolate plugins in separate processes where possible
- **Crash recovery:** Plugin crashes must not affect host
- **Parameter threading:** Thread-safe parameter updates
- **State management:** Atomic plugin state transitions

## Testing Standards

### Test Organization
- **Unit tests:** In `src/tests/` modules, test individual functions
- **Integration tests:** In `tests/` directory, test crate APIs  
- **Performance tests:** In `benches/` directory, benchmark critical paths
- **Compatibility tests:** Validate against original Pedalboard2 behavior

### Test Categories
```rust
#[cfg(test)]
mod tests {
    // Unit tests for individual functions
    #[test] fn test_audio_buffer_processing() { }
    
    // Integration tests for subsystems  
    #[test] fn test_plugin_loading_pipeline() { }
    
    // Performance tests with timing assertions
    #[test] fn test_audio_latency_requirements() { }
    
    // Compatibility tests against reference data
    #[test] fn test_patch_file_compatibility() { }
}
```

### Performance Testing
- **Latency:** Audio processing must complete within buffer time
- **Throughput:** Must handle expected plugin loads without xruns
- **Memory:** No memory leaks, bounded memory usage
- **CPU:** Profile and optimize hot paths

## Documentation Standards

### API Documentation
```rust
/// Brief one-line description of the function.
///
/// More detailed explanation of what the function does,
/// including any important behavioral notes.
///
/// # Arguments
///
/// * `param` - Description of the parameter
///
/// # Returns
///
/// Description of the return value
///
/// # Errors
///
/// Description of when/why this function returns errors
///
/// # Examples
///
/// ```rust
/// use pedalboard_audio::AudioEngine;
/// let engine = AudioEngine::new(44100, 256)?;
/// ```
pub fn example_function(param: Type) -> Result<ReturnType, Error> {
    // implementation
}
```

### Architecture Documentation
- **Decision records:** Document major architecture decisions with rationale
- **API guides:** How to use public interfaces
- **Integration guides:** How components work together
- **Migration notes:** Differences from original Pedalboard2

## Git & Collaboration Standards

### Commit Messages
```
<type>(<scope>): <brief description>

<detailed description of changes>

<footer with issue references>
```

**Types:** feat, fix, docs, style, refactor, test, chore  
**Scopes:** audio, plugins, midi, gui, core, utils, tests

### Branch Strategy
- **main:** Stable, releasable code
- **develop:** Integration branch for features  
- **feature/{name}:** Individual feature development
- **hotfix/{name}:** Critical fixes

### Code Review Requirements
- All code must be reviewed before merging
- Automated tests must pass
- Performance benchmarks must not regress
- Documentation must be updated

## Agent Coordination Standards

### Task Assignment
- Tasks assigned through PROJECT_COORDINATION.md
- Each agent responsible for specific crates/domains
- Dependencies clearly documented
- Progress tracked in shared todo system

### Cross-Agent Communication
- Shared interfaces defined in `pedalboard-utils`
- API changes require cross-agent review
- Integration points documented
- Regular sync meetings for complex dependencies

### Quality Gates
1. **Implementation:** Code compiles with no warnings
2. **Testing:** All tests pass, coverage > 80%
3. **Performance:** Benchmarks within requirements  
4. **Documentation:** Public APIs documented
5. **Review:** Approved by project manager + domain expert

## Platform Support

### Target Platforms
- **Primary:** Windows 10+, macOS 10.15+, Linux (Ubuntu 20.04+)
- **Architectures:** x86_64, ARM64 (Apple Silicon)
- **Audio APIs:** WASAPI (Windows), CoreAudio (macOS), JACK/ALSA (Linux)

### Cross-Platform Guidelines
- Use `cfg` attributes for platform-specific code
- Abstract platform differences in `pedalboard-utils`
- Test on all platforms before major releases
- Document platform-specific requirements

## Security & Safety

### Memory Safety
- No unsafe code without documented justification
- Audio buffers bounds-checked in debug builds
- Plugin sandboxing to prevent host crashes
- Input validation for all external data

### Real-Time Safety
- No heap allocations in audio thread
- No blocking operations in audio callback
- Pre-allocated error handling paths
- Graceful degradation under load

---

**Document Control:**
- **Created:** 2025-09-12 by pedalboard-project-manager
- **Authority:** Project-wide standards, all agents must follow
- **Review Cycle:** Updated as needed during development
- **Enforcement:** Code review process, CI/CD checks