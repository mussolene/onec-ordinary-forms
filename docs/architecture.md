# Architecture Notes

The repository is split by format boundary, not by CLI command.

## Layers

- `form_bin` owns the ordinary `Form.bin` section container in native C++.
  It parses, unpacks, and packs the platform section container.
- `platform_list_stream` owns the bracket/list-stream reader and writer. This
  is the native analogue of the platform `ListInStream`/`ListOutStream` layer.
- `platform_value` owns typed value fragments used inside the ordinary form
  graph: `CompositeID`, `TypeDomainPattern`, localized/formatted strings,
  colors, fonts, borders, pictures, and generic scalar values.
- `ordinary_controls` owns the internal ordinary-control codecs identified by
  `cf_form_controls8`, `cf_form_controls_position8`, and
  `cf_form_controls_info8`.
- `object_model_bridge` maps the internal graph to public `Form.xml` concepts:
  form, attributes, commands, events, controls, positions, bindings, typed
  properties, and picture sidecars.
- `main.cpp` is the current native CLI host. It should stay a thin command
  wrapper around the native layers as code moves out into dedicated headers.

## Current Direction

The next cleanup target is to move object-model XML writing/rebuild helpers out
of the CLI body into dedicated native modules and replace the remaining
hand-built control payload writer records with platform-derived codec
descriptors. The public XML must stay object-model-only; list-stream details
remain internal.

Behavioral changes should stay separate from these moves. A pure architecture
cleanup must keep native CLI arguments stable and pass the native round-trip
checks.

Byte identity is now a diagnostic, not the public release contract. The release
contract is semantic equality of the materialized ordinary form graph after
`Form.xml -> Form.bin -> Form.xml/runtime` plus strict platform validation
where the local platform is available. Broader corpus work should expand typed
descriptor coverage incrementally and record the next mismatch class in OACS.

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

The implementation consequence is concrete: new fixes should not add ad hoc
per-control writer branches unless they are adapters around descriptor rows.
A durable fix should add or update a platform-derived descriptor:

- public XML control/property/event name from `OrdinaryForm.xsd`;
- platform palette name/type from schema `appinfo`;
- internal record family (`controls`, `position`, `info`, value fragment);
- slot/default/write-condition evidence;
- dump path and build path;
- platform validation or bracket/list diff evidence.

This gives us the same shape as the platform: read many old profile shapes,
build one consistent current graph, and keep platform details either as named
schema-backed properties or as private codec defaults, never as raw public XML.

Use native gates before and after serializer work:
`make -C sidecars/onec-form-native test` and
`sidecars/onec-form-native/build/oof-native object-model-gate`. The writer
dispatch is registry-based for all supported ordinary controls. Controls found
only in managed-form documentation, such as `ПолеПериода`, are not part of the
public ordinary-form schema until platform evidence gives an ordinary-control
class id and record shape.

## Schema Boundary

The public schema boundary is intentionally narrow:

- `OrdinaryForm.xsd` is the public managed-style ordinary-form object model:
  `ChildItems`, controls, `Attributes`, `Commands`, and `Events`.
- `OrdinaryFormPalette.xsd` is the platform palette and typed property
  descriptor schema used by codec/coverage tooling.
- `PlatformConfigStructure.xsd` is codec evidence for configuration metadata,
  type-domain patterns, `CompositeID`, and platform serializer concepts.

Public `Form.xml` element and attribute names use the English vocabulary from
`OrdinaryForm.xsd`. Russian platform names are schema annotations used by tools
and documentation. They are not separate mapping files and not alternate public
XML tag names.
