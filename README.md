# onec-ordinary-forms

Python tools for splitting 1C ordinary form `Form.bin` into a Git-friendly
source package and building it back.

The public source layout mirrors managed forms:

```text
Forms/Form/Ext/Form.xml
Forms/Form/Ext/Form/Module.bsl
Forms/Form/Ext/Form/Items/<ElementName>/Picture.gif
```

`Form.xml` is the editable object model. It does not contain public
`ListStream`, `FormBin`, `LogicalStream`, or binary placeholder nodes. The
package translates the object model to and from the platform list/bracket
stream internally.

## Status

Current release: `0.2.0`.

Implemented:

- read ordinary `Form.bin` containers;
- dump readable object-model `Form.xml`;
- extract `Module.bsl` and picture sidecars;
- build `Form.bin` from `Form.xml`, `Module.bsl`, and picture sidecars;
- validate `Form.xml` against bundled XSD schemas;
- scan local EPF/ERF corpora without committing private processors or exports.

The writer is intentionally conservative while the full ordinary-form object
model is being expanded. It preserves the public source contract and writes a
canonical platform list-stream representation rather than byte-for-byte
recreating the original container layout.

## Install

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e '.[dev]'
make test
```

## CLI

Dump an ordinary form binary:

```bash
onec-ordinary-forms dump-bin \
  --bin scan-output/exported/Object/Forms/Form/Ext/Form.bin \
  --out scan-output/exported/Object/Forms/Form/Ext/Form.xml
```

This writes:

```text
scan-output/exported/Object/Forms/Form/Ext/Form.xml
scan-output/exported/Object/Forms/Form/Ext/Form/Module.bsl
scan-output/exported/Object/Forms/Form/Ext/Form/Items/<ElementName>/Picture.gif
```

Validate and format the XML:

```bash
onec-ordinary-forms validate --xml scan-output/exported/Object/Forms/Form/Ext/Form.xml
onec-ordinary-forms format-xml --xml scan-output/exported/Object/Forms/Form/Ext/Form.xml
```

Build `Form.bin` back from the object XML package:

```bash
onec-ordinary-forms build-bin \
  --xml scan-output/exported/Object/Forms/Form/Ext/Form.xml \
  --out-bin scan-output/rebuilt/Form.bin
```

Use `--asset-root` only when sidecars are not next to the XML as
`<Form.xml without suffix>/...`.

Diagnostic commands:

```bash
onec-ordinary-forms unpack-bin --bin Form.bin --out-dir scan-output/form-parts
onec-ordinary-forms pack-bin --parts-dir scan-output/form-parts --out-bin Form.bin
onec-ordinary-forms extract-elem-json --form scan-output/form-parts/Form.xml --out scan-output/form-parts/elem.json
onec-ordinary-forms scan-corpus --root "<private-processors-dir>" --out-json scan-output/corpus.json
```

`unpack-bin`, `pack-bin`, and `extract-elem-json` are diagnostics for format
research. They are not the target public source layout.

## Python API

```python
from onec_ordinary_forms import build_form_bin, dump_form_bin, validate_form_xml

dump_form_bin(
    "scan-output/exported/Object/Forms/Form/Ext/Form.bin",
    "scan-output/exported/Object/Forms/Form/Ext/Form.xml",
)

validate_form_xml("scan-output/exported/Object/Forms/Form/Ext/Form.xml")

build_form_bin(
    "scan-output/exported/Object/Forms/Form/Ext/Form.xml",
    "scan-output/rebuilt/Form.bin",
)
```

The default asset root is the XML path without the `.xml` suffix. For
`Forms/Form/Ext/Form.xml`, sidecars are read from `Forms/Form/Ext/Form/`.

## Platform Validation

For writer changes, validate rebuilt processors through the 1C platform, not
only through metadata-level checks. The helper in
`tools/platform_validate_epf.sh` runs Designer batch export and catches
malformed ordinary form streams.

Private processors, platform exports, license configuration, and generated
reports must stay in ignored local directories such as `scan-output/`, `work/`,
or `/tmp`.

## Documentation

- [Architecture](docs/architecture.md)
- [Development](docs/development.md)
- [Container validation](docs/containers.md)
- [Legacy elem-json input](docs/elem-json.md)
- [Research notes](docs/research-map.md)

## Fixture Policy

Do not commit private EPF/ERF files, CF/DT dumps, platform archives, license
files, OACS databases, generated platform exports, or customer metadata. The
`examples/fixtures/` directory is ignored except for `.gitkeep`.
