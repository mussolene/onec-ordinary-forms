# Architecture Notes

The repository is split by format boundary, not by CLI command.

## Layers

- `formbin.py` owns the ordinary `Form.bin` section container. It parses,
  unpacks, and packs sections byte-for-byte, including the service sections
  that are not decoded yet.
- `bracket.py` owns ordinary form list-stream reading. It converts that stream
  into the internal control/attribute index used by the current XML writer.
- `pipeline.py` owns orchestration between formats. For example, `dump-bin`
  means `Form.bin -> section files -> internal control index -> object-model
  XML`, but this module does not know the XML schema details.
- `cli.py` owns command-line argument parsing plus the current object-model XML
  reader/writer bridge. The public XML stays object-oriented; list-stream
  serialization is internal to the build path.
- `__init__.py` exposes the stable import wrappers: `dump_form_bin`,
  `build_form_bin`, and `validate_form_xml`.
- `corpus.py` owns portable corpus and exported-form scanning.

## Current Direction

The next cleanup target is to move object-model XML writing/rebuild helpers out
of `cli.py` into a dedicated model module and replace the remaining hand-built
control-info writer records with platform-derived codec descriptors. After
that, `cli.py` should contain only thin command wrappers.

Behavioral changes should stay separate from these moves. A pure architecture
cleanup must keep CLI arguments stable and pass the existing round-trip checks.

Byte identity is now a targeted correctness oracle, not a blanket claim for the
whole corpus. The current writer preserves physical `Form.bin` container details
and compact platform profile metadata where the platform baseline has been
observed, while the public XML remains object-model-only. Verified oracle cases
should stay byte-identical; broader UT/UPP corpus work should expand profile
coverage incrementally and record the next mismatch class in OACS.

## Platform Codec Formula

The current platform evidence points to one generic persistence path, not to a
separate public raw-stream format:

```text
Form.bin -> form stream -> ListInStream -> ordinary form object graph -> Form.xml
Form.xml -> ordinary form object graph -> ListOutStream -> form stream -> Form.bin
```

`ValueToStringInternal` and `ValueFromStringInternal` are still useful, but only
for typed value fragments that appear inside that graph: scalar values,
`TypeDomainPattern`, `CompositeID`, colors, fonts, and similar properties. They
are not, by themselves, evidence of a callable whole-form XML serializer. The
ordinary form graph is still identified by the platform `cf_form_controls8`,
`cf_form_controls_position8`, and `cf_form_controls_info8` payload families.

The implementation consequence is concrete: new fixes should not add another
control-specific branch in `ordinary_stream.py` unless it is only an adapter
around a descriptor. A durable fix should add or update a platform-derived
descriptor row:

- public XML control/property/event name from `OrdinaryForm.xsd`;
- platform palette name/type from schema `appinfo`;
- internal record family (`controls`, `position`, `info`, value fragment);
- slot/default/write-condition evidence;
- dump path and build path;
- platform validation or bracket/list diff evidence.

This gives us the same shape as the platform: read many old profiles, build one
consistent current graph, and keep compatibility details as named schema-backed
properties rather than raw sidecars.

Use `tools/audit_codec_coverage.py` before and after serializer work. It reports
the current gap between the public palette/XSD, legacy writer branches, writer
descriptor coverage, and shared slot descriptor coverage. The writer dispatch is
now registry-based for all supported ordinary controls; `PeriodChooser` remains
the explicit unsupported writer descriptor until platform evidence gives its
ordinary-control class id and record shape. Only a small core is backed by the
shared slot descriptor table so far, and that is the measured reason small
hardcoded fixes were moving the corpus slowly.

## Schema Boundary

The public schema boundary is intentionally narrow:

- `OrdinaryForm.xsd` is the editable ordinary-form object model: form root,
  controls, named properties, events, reusable value/layout types, and platform
  palette annotations.
- `PlatformConfigStructure.xsd` is codec evidence for configuration metadata,
  type-domain patterns, `CompositeID`, and platform serializer concepts.

Public `Form.xml` element and attribute names use the English vocabulary from
`OrdinaryForm.xsd`. Russian platform names are schema annotations used by tools
and documentation. They are not separate mapping files and not alternate public
XML tag names.
