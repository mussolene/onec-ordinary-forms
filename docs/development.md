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
export OOF_PLATFORM_CONTAINER=oof-1c85-licensed
tools/platform_validate_epf.sh /path/to/processor.epf
```

Use the existing licensed container first. `NETHASP_INI_PATH` is only a
fallback for creating a new throwaway container and should not be treated as
the normal local workflow.

If the named container is not running, start it from an ignored local
`nethasp.ini`:

```bash
export OOF_NETHASP_INI="<ignored-local-nethasp.ini>"
tools/platform_start_licensed_container.sh
```

The script runs 1C 8.5 and executes
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
  the native C++ implementation with `make test` and `make smoke`.
- `Release` runs on `v*` tags and manual dispatch with a tag input. It checks
  out the requested tag, runs the native release gate, packages the native
  binary and schemas, and publishes them to the GitHub release for that tag.

For a normal release:

1. Bump README/changelog status.
2. Run local native tests, smoke, release gate, and leak scan.
3. Commit the release bump.
4. Create and push an annotated `vX.Y.Z` tag.
5. Let the `Release` workflow publish native assets.

The workflow publishes only native artifacts from `dist/`. It does not use
private EPF/ERF fixtures, platform containers, license files, or local corpus
exports.

## Release Gate

`1.0.0` is a hard public contract break. Do not publish it until all of these
checks are true in current evidence:

- public `Form.xml` validation rejects legacy `<Pages>` trees, deprecated
  `version` attributes, and raw/list-stream/profile/slot/indexed platform shapes;
- native `PlatformFormObject` / ordinary form graph is the canonical dump,
  build, and mutation path;
- codec coverage classifies and handles all known ordinary-form platform
  property rows, with no unmapped, XSD-only, or no-public-XML gaps;
- representative corpora pass native semantic diff and strict Designer
  validation through `tools/platform_validate_epf.sh`;
- native release artifacts install and pass CLI smoke;
- release artifacts are published to GitHub Release only after the same gates
  pass for the tagged commit.

## Next Refactor

Continue collapsing native C++ writer gaps into named object-model concepts.
Do not reintroduce a parallel implementation or raw public XML profile surface.
