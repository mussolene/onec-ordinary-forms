# Development Notes

This repository is intentionally small while the ordinary-form model is still
being discovered.

## Target Model

The XML package should represent ordinary forms as an object model:

- form properties and module reference;
- attributes with decoded `TypeDomainPattern`;
- nested pages and controls;
- geometry and bindings with readable targets and sides;
- button actions;
- picture sidecars as files;
- binary `Form.bin` sidecar until its semantic boundary is clear.

## Verification Loop

For changes that affect build or rebuild behavior:

1. Run unit tests.
2. Run CLI smoke.
3. Use a local private fixture to dump and rebuild.
4. Validate the rebuilt platform XML/EPF with `ibcmd` in an amd64 1C container.
5. Do not commit the private fixture, compiled EPF, license data, or local logs.

## Next Refactor

Split `src/onec_ordinary_forms/cli.py` into:

- `model.py`
- `xml_dump.py`
- `bracket_writer.py`
- `assets.py`
- `types.py`
- `cli.py`
