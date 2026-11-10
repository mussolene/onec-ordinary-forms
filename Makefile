CMAKE ?= cmake
CTEST ?= ctest
CMAKE_BUILD_TYPE ?= Release
BUILD_DIR ?= build
CMAKE_CONFIGURE_ARGS ?=
CMAKE_BUILD_ARGS ?=
CTEST_ARGS ?=
PYTHON ?= python3
RELEASE_TAG ?=
RELEASE_EVIDENCE ?= docs/release-evidence.json

.PHONY: all configure build test smoke release-gate clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) $(CMAKE_CONFIGURE_ARGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --config $(CMAKE_BUILD_TYPE) $(CMAKE_BUILD_ARGS)

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --build-config $(CMAKE_BUILD_TYPE) --output-on-failure $(CTEST_ARGS)

smoke: build
	$(CTEST) --test-dir $(BUILD_DIR) --build-config $(CMAKE_BUILD_TYPE) --output-on-failure -L smoke $(CTEST_ARGS)

release-gate: build
	$(PYTHON) tools/release_gate.py --tag "$(RELEASE_TAG)" --manifest "$(RELEASE_EVIDENCE)" --binary "$(BUILD_DIR)/sidecars/onec-form-native/oof" --build-dir "$(BUILD_DIR)" --config "$(CMAKE_BUILD_TYPE)" --ctest "$(CTEST)"

clean:
	rm -rf $(BUILD_DIR) dist
