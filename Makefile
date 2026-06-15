# Pedalboard 3 - local build/test wrapper (GNU Make).
#
# Mirrors the justfile for environments that don't have `just`. Host-OS aware:
# build/test/run pick the dev preset (linux-dev / macos-dev / windows-dev);
# `release` uses the optimized preset (linux-release / macos-universal /
# windows-ci).
#
# Windows: run from a Visual Studio "x64 Native Tools" / Developer shell so
# cl.exe + Ninja are on PATH (Make cannot auto-load the VS env). For a
# zero-setup Windows build that loads the VS env for you, use build.ps1 instead.
#
# Usage:  make build | make test | make run | make release | make clean

APP_DIR := app-v3

ifeq ($(OS),Windows_NT)
  PRESET         ?= windows-dev
  RELEASE_PRESET := windows-ci
  HOST           := windows
  EXE_SUFFIX     := .exe
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    PRESET         ?= macos-dev
    RELEASE_PRESET := macos-universal
    HOST           := macos
  else
    PRESET         ?= linux-dev
    RELEASE_PRESET := linux-release
    HOST           := linux
  endif
  EXE_SUFFIX :=
endif

BUILD_DIR := build/$(PRESET)
ARTEFACT_DIR := $(BUILD_DIR)/src/Pedalboard3_artefacts/Debug

.PHONY: help build configure test run release clean format format-check

# Default target: list what's available.
help:
	@echo "Pedalboard 3 - make targets (host preset: $(PRESET))"
	@echo "  make build         configure + build the dev preset"
	@echo "  make configure     configure only (generate build files)"
	@echo "  make test          build, then run the unit tests"
	@echo "  make run           launch the built app"
	@echo "  make release       optimized build ($(RELEASE_PRESET))"
	@echo "  make format        clang-format all sources in place"
	@echo "  make format-check  clang-format --dry-run --Werror (CI lint)"
	@echo "  make clean         remove build/"

configure:
	cmake --preset $(PRESET) -S $(APP_DIR)

build: configure
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

release:
	cmake --preset $(RELEASE_PRESET) -S $(APP_DIR)
	cmake --build build/$(RELEASE_PRESET)

run: build
ifeq ($(HOST),macos)
	open "$(ARTEFACT_DIR)/Pedalboard 3.app"
else
	"$(ARTEFACT_DIR)/Pedalboard 3$(EXE_SUFFIX)"
endif

format:
	find $(APP_DIR)/src $(APP_DIR)/tests \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -not -path '*/_deps/*' -exec clang-format -i {} +

format-check:
	find $(APP_DIR)/src $(APP_DIR)/tests \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -not -path '*/_deps/*' -exec clang-format --dry-run --Werror {} +

clean:
	rm -rf build/
