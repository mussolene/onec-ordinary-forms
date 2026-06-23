.PHONY: test smoke native-smoke-gate release-gate clean FORCE

NATIVE_DIR := sidecars/onec-form-native
NATIVE_BIN := $(NATIVE_DIR)/build/oof-native

$(NATIVE_BIN): FORCE
	$(MAKE) -C $(NATIVE_DIR)

test:
	$(MAKE) -C $(NATIVE_DIR) test

smoke: $(NATIVE_BIN)
	$(NATIVE_BIN) mechanism >/dev/null
	$(NATIVE_BIN) platform-object-schema >/dev/null
	$(NATIVE_BIN) object-model-gate | grep -F -q '"status":"PASS"'
	$(NATIVE_BIN) object-model-gate | grep -F -q '"releaseReady":false'
	$(NATIVE_BIN) object-model-gate | grep -F -q '"coverageStatus":"PARTIAL"'

native-smoke-gate: test smoke

release-gate:
	@echo "release-gate is not implemented: native smoke is PASS/PARTIAL only; strict Designer and corpus validation are not run by this target." >&2
	@exit 1

clean:
	$(MAKE) -C $(NATIVE_DIR) clean
	rm -rf build dist
