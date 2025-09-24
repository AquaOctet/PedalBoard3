#!/bin/bash

# Pedalboard3 Testing Framework Runner
# Comprehensive testing script for development and CI

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PEDALBOARD_ROOT="/mnt/d/main/DEV/pedalboard"
PEDALBOARD3_ROOT="/mnt/d/main/DEV/pedalboard/pedalboard3"
TEST_RESULTS_DIR="${PEDALBOARD3_ROOT}/test_results"
BENCHMARK_RESULTS_DIR="${PEDALBOARD3_ROOT}/benchmark_results"

# Test categories
RUN_UNIT_TESTS=true
RUN_INTEGRATION_TESTS=true
RUN_BASELINE_TESTS=true
RUN_COMPATIBILITY_TESTS=true
RUN_PERFORMANCE_TESTS=true
RUN_PLUGIN_TESTS=true

# Command line argument parsing
while [[ $# -gt 0 ]]; do
    case $1 in
        --unit-only)
            RUN_INTEGRATION_TESTS=false
            RUN_BASELINE_TESTS=false
            RUN_COMPATIBILITY_TESTS=false
            RUN_PERFORMANCE_TESTS=false
            RUN_PLUGIN_TESTS=false
            shift
            ;;
        --no-baseline)
            RUN_BASELINE_TESTS=false
            shift
            ;;
        --quick)
            RUN_PERFORMANCE_TESTS=false
            RUN_PLUGIN_TESTS=false
            shift
            ;;
        --help)
            echo "Pedalboard3 Test Runner"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --unit-only      Run only unit tests"
            echo "  --no-baseline    Skip baseline tests (if original Pedalboard2 not available)"
            echo "  --quick          Skip time-consuming tests (performance, plugin scanning)"
            echo "  --help           Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo -e "${BLUE}Pedalboard3 Comprehensive Testing Framework${NC}"
echo "==========================================="
echo ""

# Create test results directories
mkdir -p "$TEST_RESULTS_DIR"
mkdir -p "$BENCHMARK_RESULTS_DIR"

# Function to print section headers
print_section() {
    echo ""
    echo -e "${BLUE}=== $1 ===${NC}"
    echo ""
}

# Function to run tests with error handling
run_test_category() {
    local category=$1
    local description=$2
    
    echo -e "${YELLOW}Running $description...${NC}"
    
    if cargo test --manifest-path="${PEDALBOARD3_ROOT}/Cargo.toml" "$category" -- --nocapture; then
        echo -e "${GREEN}✓ $description passed${NC}"
        return 0
    else
        echo -e "${RED}✗ $description failed${NC}"
        return 1
    fi
}

# Pre-flight checks
print_section "Pre-flight Checks"

echo "Checking Rust toolchain..."
if ! command -v cargo &> /dev/null; then
    echo -e "${RED}✗ Cargo not found. Please install Rust toolchain.${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Cargo found: $(cargo --version)${NC}"

echo "Checking project structure..."
if [ ! -f "${PEDALBOARD3_ROOT}/Cargo.toml" ]; then
    echo -e "${RED}✗ Cargo.toml not found in ${PEDALBOARD3_ROOT}${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Project structure valid${NC}"

echo "Checking for original Pedalboard2..."
ORIGINAL_PEDALBOARD="${PEDALBOARD_ROOT}/Pedalboard2-64bit.exe"
if [ -f "$ORIGINAL_PEDALBOARD" ]; then
    echo -e "${GREEN}✓ Original Pedalboard2 found: ${ORIGINAL_PEDALBOARD}${NC}"
    BASELINE_AVAILABLE=true
else
    echo -e "${YELLOW}⚠ Original Pedalboard2 not found - baseline tests will be limited${NC}"
    BASELINE_AVAILABLE=false
    if [ "$RUN_BASELINE_TESTS" = true ]; then
        echo "  Use --no-baseline to skip baseline tests"
    fi
fi

# Build the project first
print_section "Building Project"
echo "Building Pedalboard3..."
if cargo build --manifest-path="${PEDALBOARD3_ROOT}/Cargo.toml"; then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi

# Test execution
TEST_FAILURES=0

# Unit Tests
if [ "$RUN_UNIT_TESTS" = true ]; then
    print_section "Unit Tests"
    
    # Test individual crates
    for crate_dir in "${PEDALBOARD3_ROOT}"/crates/*/; do
        if [ -f "${crate_dir}Cargo.toml" ]; then
            crate_name=$(basename "$crate_dir")
            echo "Testing crate: $crate_name"
            
            if cargo test --manifest-path="${crate_dir}Cargo.toml" -- --nocapture; then
                echo -e "${GREEN}✓ $crate_name tests passed${NC}"
            else
                echo -e "${RED}✗ $crate_name tests failed${NC}"
                ((TEST_FAILURES++))
            fi
        fi
    done
fi

# Integration Tests
if [ "$RUN_INTEGRATION_TESTS" = true ]; then
    print_section "Integration Tests"
    
    if run_test_category "audio_processing" "Audio Processing Integration"; then
        :
    else
        ((TEST_FAILURES++))
    fi
fi

# Baseline Tests (if original Pedalboard2 available)
if [ "$RUN_BASELINE_TESTS" = true ]; then
    print_section "Baseline Validation Tests"
    
    if [ "$BASELINE_AVAILABLE" = true ]; then
        if run_test_category "baseline_testing" "Baseline Validation"; then
            :
        else
            ((TEST_FAILURES++))
        fi
    else
        echo -e "${YELLOW}⚠ Skipping baseline tests - original Pedalboard2 not available${NC}"
    fi
fi

# Compatibility Tests
if [ "$RUN_COMPATIBILITY_TESTS" = true ]; then
    print_section "Compatibility Validation Tests"
    
    if run_test_category "compatibility_validation" "Compatibility Validation"; then
        :
    else
        ((TEST_FAILURES++))
    fi
fi

# Plugin Tests
if [ "$RUN_PLUGIN_TESTS" = true ]; then
    print_section "Plugin Compatibility Tests"
    
    if run_test_category "plugin_compatibility" "Plugin Compatibility"; then
        :
    else
        ((TEST_FAILURES++))
    fi
fi

# Performance Tests
if [ "$RUN_PERFORMANCE_TESTS" = true ]; then
    print_section "Performance Benchmarks"
    
    echo "Running performance benchmarks..."
    if cargo bench --manifest-path="${PEDALBOARD3_ROOT}/Cargo.toml"; then
        echo -e "${GREEN}✓ Performance benchmarks completed${NC}"
    else
        echo -e "${RED}✗ Performance benchmarks failed${NC}"
        ((TEST_FAILURES++))
    fi
    
    # Save benchmark results with timestamp
    TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
    cp -r "${PEDALBOARD3_ROOT}/target/criterion" "${BENCHMARK_RESULTS_DIR}/benchmark_${TIMESTAMP}" 2>/dev/null || true
fi

# Test Summary
print_section "Test Summary"

if [ $TEST_FAILURES -eq 0 ]; then
    echo -e "${GREEN}🎉 All tests passed successfully!${NC}"
    echo ""
    echo "Test Results:"
    echo "  ✓ Unit Tests: $([ "$RUN_UNIT_TESTS" = true ] && echo "PASSED" || echo "SKIPPED")"
    echo "  ✓ Integration Tests: $([ "$RUN_INTEGRATION_TESTS" = true ] && echo "PASSED" || echo "SKIPPED")"
    echo "  ✓ Baseline Tests: $([ "$RUN_BASELINE_TESTS" = true ] && [ "$BASELINE_AVAILABLE" = true ] && echo "PASSED" || echo "SKIPPED")"
    echo "  ✓ Compatibility Tests: $([ "$RUN_COMPATIBILITY_TESTS" = true ] && echo "PASSED" || echo "SKIPPED")"
    echo "  ✓ Plugin Tests: $([ "$RUN_PLUGIN_TESTS" = true ] && echo "PASSED" || echo "SKIPPED")"
    echo "  ✓ Performance Tests: $([ "$RUN_PERFORMANCE_TESTS" = true ] && echo "PASSED" || echo "SKIPPED")"
    
    exit 0
else
    echo -e "${RED}❌ $TEST_FAILURES test categories failed${NC}"
    echo ""
    echo "Please review the test output above for details."
    echo "Test results are saved in: $TEST_RESULTS_DIR"
    
    exit 1
fi