.PHONY: test smoke format-xml v1-release-gate clean

PYTHON ?= python3

test:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests

smoke:
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli build-bin --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli unpack-bin --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli pack-bin --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli dump-bin --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli scan-corpus --help

format-xml:
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli format-xml --xml src/onec_ordinary_forms/schemas/OrdinaryForm.xsd
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli format-xml --xml src/onec_ordinary_forms/schemas/PlatformConfigStructure.xsd

v1-release-gate:
	PYTHONPATH=src $(PYTHON) tools/v1_release_gate.py --repo .

clean:
	find . -type d -name __pycache__ -prune -exec rm -rf {} +
	find . -type d -name '*.egg-info' -prune -exec rm -rf {} +
	rm -rf build dist .pytest_cache
