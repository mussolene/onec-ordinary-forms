# `elem-json` Input

`--elem-json` is the legacy semantic index consumed by the current
`dump` command. It is not a platform file emitted by `ibcmd`.

The original prototype used a private experimental extractor output shaped like
this:

- `props`: form attributes/requisites with `name`, `id`, and raw
  `TypeDomainPattern` fragments;
- `commands`: command/action records when the extractor can see them;
- `data`: path-addressed raw form nodes, including page records and element
  records with stable numeric `id` values;
- `tree`: nested visual element structure with `name`, `type`, `page`, and
  `child` arrays.

A sanitized synthetic example is committed at:

```text
examples/elem-json/minimal.json
```

Do not commit real extractor output from customer EPF/ERF files. Real
`elem-json` can contain object names, form text, action names, and enough
structure to disclose private business logic.

## Current Status

There are now three related paths:

- `dump --elem-json`: the early object-model path. It still needs the legacy
  semantic index because it maps raw ordinary form nodes to attributes,
  controls, pages, bindings, and actions.
- `unpack-bin` / `pack-bin`: the platform-export path for ordinary forms that
  arrive from `ibcmd` as `Forms/<Form>/Ext/Form.bin`. This can split and
  rebuild the sectioned binary stream without `elem-json`.
- `dump-bin`: the convenience path for `Form.bin -> OrdinaryForm.xml`. It
  unpacks `Form.bin`, extracts `elem-json` from the bracket stream, and runs
  the object-model dump.

The intermediate extractor is also available directly:

```bash
PYTHONPATH=src python -m onec_ordinary_forms.cli extract-elem-json \
  --form scan-output/form-parts/Form.xml \
  --out scan-output/form-parts/elem.json
```

The extractor is intentionally conservative. It parses the bracket stream,
finds `Pattern` fragments, page captions, and element-like nodes with geometry,
then writes the legacy `props`/`commands`/`data`/`tree` shape. The next
implementation step is to replace these heuristics with a fuller semantic
decoder for all ordinary-form controls and properties.

## Minimal Shape

```json
{
  "props": [],
  "commands": [],
  "data": {
    "-pages-": ["Main"],
    "Main": {"id": "10", "raw": []}
  },
  "tree": []
}
```

The full object-model dump currently expects enough `data` and `tree` content
to identify element ids, page membership, geometry, and bindings. Missing raw
nodes produce a sparse but valid XML model; they do not recover information
that was never decoded.
