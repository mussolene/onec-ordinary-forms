# onec-ordinary-forms

Experimental Python tools for studying and editing 1C ordinary forms as a
Git-friendly object XML package.

The project targets old ordinary forms that platform XML export still stores as
`Forms/<Form>/Ext/Form.bin`. The long-term goal is the same source layout used
for managed forms: readable `Form.xml`, separate `Module.bsl`, and picture
sidecars next to the form.

## Status

This is alpha research tooling. Reading and object-model XML dumping are ahead
of writing.

Implemented:

- parse the sectioned ordinary `Form.bin` container;
- decode the current ordinary form bracket/list stream enough to build readable
  `Form.xml`;
- write `Module.bsl` and picture files as sidecars;
- validate the public XML shape with bundled XSD schemas;
- scan local EPF/ERF corpora without committing private processors or exports.

In progress:

- object-only `Form.xml` to `Form.bin` serialization;
- complete typed properties for all ordinary controls;
- platform-level validation for edited forms.

The current public `Form.xml` intentionally does not expose low-level
`ListStream`, `FormBin`, `LogicalStream`, or binary placeholder nodes. The
writer must reconstruct the platform stream internally from the object model.

## Install

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e '.[dev]'
make test
```

For direct source execution without installation:

```bash
PYTHONPATH=src python -m unittest discover -s tests
PYTHONPATH=src python -m onec_ordinary_forms.cli --help
```

## CLI

```bash
onec-ordinary-forms dump-bin --help
onec-ordinary-forms build-bin --help
onec-ordinary-forms unpack-bin --help
onec-ordinary-forms pack-bin --help
onec-ordinary-forms extract-elem-json --help
onec-ordinary-forms scan-corpus --help
```

`dump-bin` is the main object XML path:

```bash
PYTHONPATH=src python -m onec_ordinary_forms.cli dump-bin \
  --bin scan-output/exported/Object/Forms/Form/Ext/Form.bin \
  --out scan-output/exported/Object/Forms/Form/Ext/Form.xml
```

With a managed-form-like output path, sidecars are written next to the form:

```text
Forms/Form/Ext/Form.xml
Forms/Form/Ext/Module.bsl
Forms/Form/Ext/Items/<ElementName>/Picture.gif
```

`unpack-bin` and `pack-bin` are lower-level diagnostics for the sectioned
binary container. They are useful for no-op stream investigation, but they are
not the target public source layout.

## Validation

Run the unit suite for parser and schema checks:

```bash
make test
```

For writer work, validate rebuilt processors through the 1C platform, not only
through metadata-level checks. The helper in `tools/platform_validate_epf.sh`
runs Designer batch export and catches malformed ordinary form streams.

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
