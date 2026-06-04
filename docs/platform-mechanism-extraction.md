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
```

Historical note: the old post-processor for mechanism JSON/Markdown was removed
with the previous implementation. Use git history if that research helper is
needed again; current product code should stay native C++.

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

The proven GetData boundary is not symmetric for all three formats:

- `cf_form_controls_position8` is count plus 32-byte records.
- `cf_form_controls_info8` is count plus 16-byte records copied from the info
  linked list.
- `cf_form_controls8` is returned as an existing raw payload file/HGLOBAL in
  `FUN_00270da0`; do not model it as a fixed 40-byte record list at that
  boundary. Native tests may use a diagnostic fixture chunk to exercise the
  transfer envelope, but that fixture is not a platform control record shape.

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

`LocalWString`/`FormattedString` localization support must be extracted from
the platform serializers themselves. The local Linux library set referenced
these symbols from `dsgnfrm.so`, `mngbase.so`, and `mngui.so`, but only proved
the platform object names and call surface. The platform container
`ghcr.io/mussolene/1c-developer:8.5.1.1343` provides the serializer bodies in
`core85.so`; selected libraries were copied only to ignored
`work/platform85-container-libs/` for inspection.

Container `core85.so` definitions:

- `core::FormattedString::serialize(core::ListOutStream&) const`: `0x5347b0`.
- `core::FormattedString::deserialize(core::ListInStream&)`: `0x534800`.
- `core::LocalWString::serialize(core::ListOutStream&) const`: `0x57c520`.
- `core::LocalWString::deserialize(core::ListInStream&)`: `0x57c610`.
- `core::LocalWString::addItem(...)`: `0x579d40`.

The sanitized platform-derived shape is:

- `FormattedString` writes a list/version marker `1`, then delegates to
  `LocalWString`, then writes the formatting flag stored at object offset
  `0x40`.
- `LocalWString` writes a list/version marker `1`, then an item count, then
  per localized item writes two `BasicString<char16_t>` values through the
  list stream. The first item is stored inline on the object and additional
  items use vector entries with stride `0x30`; each entry contains the two
  strings at offsets `0x0` and `0x18`.
- `LocalWString::deserialize` reads the same version/count pair, resets the
  object, allocates additional vector entries when count is greater than one,
  and reads the two strings for each item through the platform list-stream
  string reader.

Do not port an old localized-text helper shape as if it were platform evidence.
Native `platform_value` work for localized values must be derived from
`core85.so` serializer evidence or a live platform oracle.

The local `.so` resource extraction found no embedded XML/XSD fragments in
`dsgnfrm.so`, `frmcore.so`, `mngui.so`, or `mngbase.so`. The model catalog from
strings/imports found 12 metadata object candidates and 25 type tree
candidates in the same local library set. If a fuller platform root with
`.res`/`.hbk` resources is available, rerun the extractor against that root.

## Native Engine Mapping

- `list_stream`: parse and write platform bracket/list stream syntax.
- `platform_value`: implement `CompositeID`, `TypeDomainPattern`,
  `core::LocalWString`, `core::FormattedString`, generic scalar values,
  color/font/border/picture records from platform serializers.
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
