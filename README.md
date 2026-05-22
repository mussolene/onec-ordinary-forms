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

## CLI

```bash
onec-ordinary-forms dump --help
onec-ordinary-forms rebuild --help
```

For direct source execution:

```bash
PYTHONPATH=src python -m onec_ordinary_forms.cli dump --help
```

## Fixture Policy

Do not commit private EPF files, platform archives, license files, `nethasp.ini`,
OACS databases, or customer configuration dumps. Put local fixtures under
`examples/fixtures/`; this directory is ignored except for `.gitkeep`.
