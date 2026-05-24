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
- no public low-level `ListStream`, `FormBin`, `LogicalStream`, or binary
  placeholder nodes.

The parser/writer boundary is internal: object XML is the source format, and
the package code is responsible for translating that model to and from the
platform list/bracket stream.

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
export NETHASP_INI_PATH="<local-nethasp.ini>"
tools/platform_validate_epf.sh /path/to/processor.epf
```

The script runs 1C 8.5 in the amd64 container and executes
`/DumpExternalDataProcessorOrReportToFiles`. That platform command
deserializes ordinary `Form.bin` deeply enough to reject malformed bracket/list
streams. Logs and generated dumps stay under ignored `scan-output/`.

For documentation-only changes, run at least the unit tests that protect the
public XML contract and the CLI smoke checks. Full platform validation is not
required unless the change affects parser, writer, schema, or packaging
behavior.

## GitHub Automation

The repository has two GitHub Actions workflows:

- `CI` runs on pushes to `main`, pull requests, and manual dispatch. It tests
  Python 3.10, 3.11, and 3.12, then runs CLI smoke, builds the package, checks
  the built artifacts with `twine`, and uploads the Python 3.12 artifacts.
- `Release` runs on `v*` tags and manual dispatch with a tag input. It checks
  out the requested tag, installs release dependencies, runs tests and smoke,
  builds the package, checks artifacts, and publishes them to the GitHub
  release for that tag.

For a normal release:

1. Bump `pyproject.toml`, `src/onec_ordinary_forms/__init__.py`, and README
   status.
2. Run local tests, smoke, package build, `twine check`, and leak scan.
3. Commit the release bump.
4. Create and push an annotated `vX.Y.Z` tag.
5. Let the `Release` workflow publish wheel and sdist assets.

The workflow publishes only package artifacts from `dist/`. It does not use
private EPF/ERF fixtures, platform containers, license files, or local corpus
exports.

## Next Refactor

Split `src/onec_ordinary_forms/cli.py` into:

- `model.py`
- `xml_dump.py`
- `bracket_writer.py`
- `assets.py`
- `types.py`
- `cli.py`
