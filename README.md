# onec-ordinary-forms

Experimental Python tools for decomposing 1C ordinary forms into an editable
object-model XML package and rebuilding selected semantic edits back into the
platform form stream.

The project started from a real ordinary form exported from an EPF and is
intended to grow into a stable Git-friendly representation for 1C ordinary
forms, similar in spirit to platform XML for managed forms.

## Current Capabilities

- Dump ordinary form `form`, `Form.bin`, and optional `Module.bsl`.
- Emit object-model XML with attributes, nested pages/items, geometry,
  bindings, button actions, picture sidecars, and module sidecar.
- Keep pictures as external files, for example
  `Items/<ElementName>/Picture.gif`, not inline base64.
- Rebuild using explicit base `form`/`Form.bin` streams.
- Apply the first semantic write-back slice: root form title.
- Restore `Module.bsl` from the XML package.
- Scan a private EPF/ERF corpus and classify platform-exported ordinary forms
  without hardcoding local paths into reports.

## Known Gaps

- Element/page title write-back by stable `id`.
- Geometry and binding write-back.
- Attribute `TypeDomainPattern` write-back.
- Picture sidecar re-embedding into the ordinary-form stream.
- Decision on which `Form.bin` fields should be decoded semantically.
- Regression corpus for more ordinary form control types.

## Development

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e '.[dev]'
make test
```

Without creating a virtual environment:

```bash
PYTHONPATH=src python -m unittest discover -s tests
PYTHONPATH=src python -m onec_ordinary_forms.cli --help
```

Platform/container notes live in [docs/containers.md](docs/containers.md).
Research and transfer notes live in [docs/research-map.md](docs/research-map.md).
Module boundaries are summarized in [docs/architecture.md](docs/architecture.md).
The legacy `dump --elem-json` input is documented in [docs/elem-json.md](docs/elem-json.md).

## CLI

```bash
onec-ordinary-forms dump --help
onec-ordinary-forms rebuild --help
onec-ordinary-forms unpack-bin --help
onec-ordinary-forms pack-bin --help
onec-ordinary-forms extract-elem-json --help
onec-ordinary-forms dump-bin --help
onec-ordinary-forms scan-corpus --help
```

For direct source execution:

```bash
PYTHONPATH=src python -m onec_ordinary_forms.cli dump --help
```

`dump --elem-json` currently consumes a legacy semantic index. See
[docs/elem-json.md](docs/elem-json.md) and `examples/elem-json/minimal.json`
for the safe committed shape. For current `ibcmd` exports that contain only
`Form.bin`, use `dump-bin` to split the binary stream, extract `elem-json`, and
write object-model XML in one step.

## Ordinary `Form.bin` Sections

For platform XML exports that contain ordinary forms as
`Forms/<Form>/Ext/Form.bin`, split the binary stream into editable section
files:

```bash
PYTHONPATH=src python -m onec_ordinary_forms.cli unpack-bin \
  --bin scan-output/exported/Object/Forms/Form/Ext/Form.bin \
  --out-dir scan-output/form-parts
```

The output directory contains `Module.bsl`, `Form.xml`, service section files,
and `Form.bin.parts.json` for exact reassembly:

```bash
PYTHONPATH=src python -m onec_ordinary_forms.cli pack-bin \
  --parts-dir scan-output/form-parts \
  --out-bin scan-output/rebuilt/Form.bin
```

To produce object-model XML directly from the ordinary form binary:

```bash
PYTHONPATH=src python -m onec_ordinary_forms.cli dump-bin \
  --bin scan-output/exported/Object/Forms/Form/Ext/Form.bin \
  --out scan-output/exported/Object/Forms/Form/Ext/Form.xml
```

With that output path, sidecars follow the same layout as managed forms:
`Form/Module.bsl` and `Form/Items/<ElementName>/Picture.gif`.

To inspect or reuse the intermediate legacy index:

```bash
PYTHONPATH=src python -m onec_ordinary_forms.cli extract-elem-json \
  --form scan-output/form-parts/Form.xml \
  --out scan-output/form-parts/elem.json
```

## Corpus Scanning

The scanner is intentionally split from platform export. It can list private
`.epf`/`.erf` files directly, and it can classify a directory that was already
exported by `ibcmd config export --file`.

```bash
PYTHONPATH=src python -m onec_ordinary_forms.cli scan-corpus \
  --root "<private-processors-dir>" \
  --name-regex 'обычн|obychn|8\.2|8\.1' \
  --limit 50 \
  --out-json scan-output/corpus.json
```

After exporting several candidates to a temporary directory:

```bash
PYTHONPATH=src python -m onec_ordinary_forms.cli scan-corpus \
  --root "<private-processors-dir>" \
  --exported-root scan-output/exported \
  --out-json scan-output/exported-forms.json
```

## Fixture Policy

Do not commit private EPF files, platform archives, license files, `nethasp.ini`,
OACS databases, or customer configuration dumps. Put local fixtures under
`examples/fixtures/`; this directory is ignored except for `.gitkeep`.
