.PHONY: test smoke clean

PYTHON ?= python3

test:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests

smoke:
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli dump --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli rebuild --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli unpack-bin --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli pack-bin --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli extract-elem-json --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli dump-bin --help
	PYTHONPATH=src $(PYTHON) -m onec_ordinary_forms.cli scan-corpus --help

clean:
	find . -type d -name __pycache__ -prune -exec rm -rf {} +
	find . -type d -name '*.egg-info' -prune -exec rm -rf {} +
	rm -rf build dist .pytest_cache
