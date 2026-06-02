# Platform Mechanism Extraction

This document records the current extraction boundary for the ordinary-form
platform mechanism. The raw Ghidra output, platform binaries, and decompiled
bodies stay in ignored `work/` or `scan-output/` directories. Tracked files
contain only the reproducible extraction tool and sanitized mechanism map.

## Extraction Command

```bash
tools/ghidra_decompile_form_functions.sh \
  work/platform85-libs/dsgnfrm.so \
  work/ghidra-platform-mechanism-current

python3 tools/extract_platform_mechanism.py \
  --decompile-json work/ghidra-platform-mechanism-current/dsgnfrm.so.ghidra-decompile.json \
  --xrefs-json work/ghidra-runtime-graph/xrefs/dsgnfrm.so.ghidra-xrefs.json \
  --lib work/platform85-libs/dsgnfrm.so \
  --lib work/platform85-libs/frmcore.so \
  --lib work/platform85-libs/mngui.so \
  --lib work/platform85-libs/mngbase.so \
  --json-out scan-output/platform-mechanism-current/mechanism.json \
  --md-out scan-output/platform-mechanism-current/mechanism.md
```

## Platform Entry Points

- `00255f70`: write-side persistence entry. It constructs
  `core::ListOutStream::ListOutStream`.
- `00256510`: read-side persistence entry. It constructs
  `core::ListInStream::ListInStream`.
- `002709e0`, `00270da0`, `00270fe0`: ordinary-control triplet entries. Each
  calls `wbase::cf_form_controls8`,
  `wbase::cf_form_controls_position8`, and
  `wbase::cf_form_controls_info8`.
- `002c9430`: ordinary-control info entry. It calls
  `wbase::cf_form_controls_info8` without the full triplet.

The xref surface also shows data-table references for the same three
`cf_form_controls*` factories. That means the native implementation should
model them as a descriptor registry, not as ad hoc per-form fallbacks.

## Form Object Surface

The `dsgnfrm.so` string surface around the ordinary-form mechanism includes
these form/designer/runtime objects:

- transfer/enumeration: `FormDataObject`, `FormFormatEnumerator`;
- designer document/view: `FormDesDoc`, `FormDesView`,
  `FormDesDocFactory`, `CustomFormLoader`, `ControlSite`;
- runtime document/view: `FormDocument`, `FormDocumentView`,
  `FormDocumentFactory`, `FormDocumentMoxelFactory`;
- services/properties/undo: `FormDesignerService`, `FormProperties`,
  `FormDocPropertiesWrapper`, `FormUndoManager`;
- support dialogs/sites: `FormDesignerSite`, `TestForm`, `ControlSelDlg`,
  `FieldsDialog`, `PropertiesEditDialog`, `GridParametersDialog`.

The C++ engine should keep the same plain separation: transfer object,
descriptor enumerator, designer graph, runtime graph, and control-site bridge.

## Transfer Formats

- `cf_form_controls8`: `0x2500`.
- `cf_form_controls_position8`: `0x5500`.
- `cf_form_controls_info8`: `0x9d00`.
- Common transfer count prefix: 4 bytes.
- `cf_form_controls_info8` transfer record: 16 bytes.
- `cf_form_controls_position8` transfer record: 32 bytes.
- Format entry record: 40 bytes.

## Core Value Surface

The relevant platform libraries import these serializers/value types around
the ordinary-form mechanism:

- stream layer: `ListInStream`, `ListOutStream`;
- typed metadata/value layer: `TypeDomainPattern`, `CompositeID`,
  `GenericValue`, `LocalWString`, `FormattedString`;
- UI value layer: `Color`, `Font`, `V8Border`, `V8Picture`;
- scalar support: `ShortCut`, `Date`, `Numeric`;
- persistence support: `IInPersistenceStorage`, `IOutPersistenceStorage`.

This is the order to port into the native engine. Starting with controls
without these value serializers repeats the current slot-guessing problem.

The local `.so` resource extraction found no embedded XML/XSD fragments in
`dsgnfrm.so`, `frmcore.so`, `mngui.so`, or `mngbase.so`. The model catalog from
strings/imports found 12 metadata object candidates and 25 type tree
candidates in the same local library set. If a fuller platform root with
`.res`/`.hbk` resources is available, rerun the extractor against that root.

## Native Engine Mapping

- `list_stream`: parse and write platform bracket/list stream syntax.
- `platform_value`: implement `CompositeID`, `TypeDomainPattern`,
  localized strings, generic scalar values, color/font/border/picture records.
- `form_bin`: split and join the ordinary `Form.bin` section container.
- `ordinary_controls`: implement the descriptor registry for
  `cf_form_controls8`, `cf_form_controls_position8`, and
  `cf_form_controls_info8`.
- `object_model_bridge`: map descriptor records to public `Form.xml` concepts
  only after the internal platform graph is complete enough.

## Direct Port Order

1. Copy the current platform constants and object surfaces into native code.
2. Implement `ListInStream`/`ListOutStream` behavior first.
3. Implement core typed values exactly as observed by platform serializers.
4. Implement `FormDataObject` and `FormFormatEnumerator` semantics for the
   three `cf_form_controls*` transfer formats.
5. Implement `FormDesDoc/FormDesView` and `FormDocument/FormDocumentView`
   record graph structures only where persistence evidence requires them.
