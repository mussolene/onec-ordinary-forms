CMAKE ?= cmake
CTEST ?= ctest
CMAKE_BUILD_TYPE ?= Release
BUILD_DIR ?= build
CMAKE_CONFIGURE_ARGS ?=
CMAKE_BUILD_ARGS ?=
CTEST_ARGS ?=

.PHONY: all configure build test smoke native-smoke-gate release-gate clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) $(CMAKE_CONFIGURE_ARGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --config $(CMAKE_BUILD_TYPE) $(CMAKE_BUILD_ARGS)

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --build-config $(CMAKE_BUILD_TYPE) --output-on-failure -L regression $(CTEST_ARGS)

smoke: build
	$(CTEST) --test-dir $(BUILD_DIR) --build-config $(CMAKE_BUILD_TYPE) --output-on-failure -L smoke $(CTEST_ARGS)

native-smoke-gate: test smoke

release-gate:
	@echo "release-gate is not implemented: native smoke is PASS/PARTIAL only; strict Designer and corpus validation are not run by this target." >&2
	@exit 1

clean:
	rm -rf $(BUILD_DIR) sidecars/onec-form-native/build dist
