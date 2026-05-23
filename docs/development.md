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
4. Validate the rebuilt EPF/ERF by asking the platform to dump it with
   `tools/platform_validate_epf.sh`.
5. Do not commit the private fixture, compiled EPF, license data, or local logs.

`ibcmd config load` plus `ibcmd config check` is useful for metadata-level
checks, but it is not sufficient for ordinary `Form.bin` writer validation: it
can accept an EPF whose ordinary form later fails with "Ошибка формата потока".
The stricter check is Designer batch mode:

```bash
. .agent/local/nethasp.env
tools/platform_validate_epf.sh /path/to/processor.epf
```

The script runs 1C 8.5 in the amd64 container and executes
`/DumpExternalDataProcessorOrReportToFiles`. That platform command
deserializes ordinary `Form.bin` deeply enough to reject malformed bracket/list
streams. Logs and generated dumps stay under ignored `scan-output/`.

## Next Refactor

Split `src/onec_ordinary_forms/cli.py` into:

- `model.py`
- `xml_dump.py`
- `bracket_writer.py`
- `assets.py`
- `types.py`
- `cli.py`
