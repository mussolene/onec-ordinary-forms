.PHONY: test smoke release-gate clean

NATIVE_DIR := sidecars/onec-form-native
NATIVE_BIN := $(NATIVE_DIR)/build/oof-native

$(NATIVE_BIN):
	$(MAKE) -C $(NATIVE_DIR)

test:
	$(MAKE) -C $(NATIVE_DIR) test

smoke: $(NATIVE_BIN)
	$(NATIVE_BIN) mechanism >/dev/null
	$(NATIVE_BIN) platform-object-schema >/dev/null
	$(NATIVE_BIN) object-model-gate | grep -F -q '"status":"PASS"'

release-gate: test smoke

clean:
	$(MAKE) -C $(NATIVE_DIR) clean
	rm -rf build dist
