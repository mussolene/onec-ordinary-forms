# Ordinary Form Pattern Audit

This note records the current architectural conclusion for ordinary form
reader/writer work. It is deliberately about the pattern, not about one more
control-specific fix.

## Objective

Build ordinary form source as a typed object model:

```text
Form.bin container
  -> ordinary form payload
  -> ListInStream / cf_form_controls8 graph
  -> typed Form.xml + Module.bsl + picture files

typed Form.xml + Module.bsl + picture files
  -> typed ordinary form graph
  -> descriptor-driven ListOutStream / cf_form_controls8 graph
  -> ordinary form payload
  -> Form.bin container
```

The public XML boundary is the object model. Platform list-stream details are
codec internals.

## Evidence Summary

- Platform scans and LD audit evidence agree on a generic persistence path:
  `ListInStream`, `ListOutStream`, `TypeDomainPattern`, `CompositeID`, and the
  ordinary control payload families `cf_form_controls8`,
  `cf_form_controls_position8`, `cf_form_controls_info8`.
- 8.2 and 8.5 differ in implementation details, but the observed external
  transfer contracts for ordinary control position/info records stay compatible:
  count plus 16-byte records for the transfer payloads.
- Interactive and corpus checks showed that platform save/load can canonicalize
  old record shapes. Therefore byte identity against an old source shape is a
  diagnostic, not the public contract.
- Strict Designer load alone is not enough: missing metadata types in a
  validation infobase can make platform redump lose table/attribute/control
  semantics even when the stream loads.

## Current Product Pattern

The repository now has the right foundation:

- all supported ordinary controls are present in the public palette/XSD set;
- writer dispatch is registry-based instead of legacy per-control branch
  dispatch;
- shared control info descriptors exist for the supported controls;
- writer template fallback has been removed;
- public control-level raw profile leaks were already reduced for CommandBar,
  PictureDecoration, and Panel by promoting them into named concepts such as
  `CommandSource`, `PictureStyle`, and `PanelLayout`.
- the public position `dimensionProfile` selector and extra dimension
  `slotN` names were removed; writer profile selection now uses named
  dimensions, sections, and internal descriptor checks.
- the constant position `unit="form"` marker and coordinate `slotN` fallback
  were removed from the public position/binding dump path.

The current loop comes from the remaining mixed model:

- some paths build records from descriptors and named XML;
- other paths still preserve stream shape through public profile-like XML;
- tests sometimes assert slot/profile shapes directly, which locks in the
  wrong boundary;
- codec coverage currently proves that builders exist, not that every public
  property is mapped to a platform slot.

## Fresh Re-Audit: 2026-05-31

This re-audit used the current repository state, recent commit history, OACS
memory/context, and local coverage checks. The conclusion did not change, but
the concrete failure point is now sharper.

Current facts:

- Recent history shows the oscillation clearly: many commits first preserved
  compact/root/control/profile shapes to chase byte diffs, then later commits
  removed those same public shapes (`Form/SerializationProfile`,
  `dimensionProfile`, `slotN`, `unit`, `dimensionSegments`,
  `secondaryDimensionMarker`) and replaced some of them with named concepts or
  canonical writer defaults.
- The writer dispatch itself is no longer the main problem. The current codec
  coverage audit reports 26 public controls, 26 writer descriptors, 26 shared
  info descriptors, zero legacy writer branches, and zero template fallback
  tokens.
- The remaining problem is property and graph coverage. The same audit still
  reports many `xsdOnlyProperties`, especially on `InputField`, `Table`,
  `Panel`, `CommandBar`, `ProgressBar`, and chart/schema controls. That means
  "control is supported" is not the same as "all public platform properties of
  this control are round-tripped".
- `PositionType` used to expose codec-shaped attributes:
  `layoutPreTail`, `primaryDimensionMarker`, and `layoutTail`. These are now
  removed from public schema/dump/build; the writer derives the required record
  shape from named bindings/sections/layout order and internal descriptor
  defaults.
- `RootPanelLayout` and `PanelLayout` are better than a generic
  `SerializationProfile`, but they still contain raw-shaped residues such as
  `dependencyTail`, `pageLayoutHeader`, `postLayoutTailBeforeColor`,
  `postLayoutTailAfterColor`, and dependency group `prefix`/`header`. These
  must be classified before they are allowed to remain public.
- Platform evidence in OACS keeps pointing to the same persistence mechanism:
  ordinary forms are serialized through `ListInStream`/`ListOutStream`,
  `TypeDomainPattern`, `CompositeID`, and the `cf_form_controls8`,
  `cf_form_controls_position8`, `cf_form_controls_info8` payload families.
  LD_AUDIT confirms this path for the all-controls fixture. There is no
  evidence that ordinary form persistence is Delphi DFM or another separate
  public form file format.

The actual functional formula is:

```text
container Form.bin
  -> form list-stream
  -> platform ordinary form graph
     controls + info + position + attributes + actions + type-domain values
  -> public typed Form.xml

public typed Form.xml
  -> ordinary form graph
  -> one canonical internally consistent ListOutStream generation
  -> form list-stream
  -> container Form.bin
```

The important word is `graph`. A control record is not independent from the
root panel, attributes table, event/action table, table-column editor records,
UUID identity, type-domain references, and layout dependency graph. Strict-load
failures after editing "just one column type" can still be caused by a broken
cross-record relation, not by the visible column property alone.

For table columns, the right model is also graph-based: a column may have an
`ElementControl` editor that is effectively a nested typed editor/control
concept, such as an input field or choice field. It should be represented as a
named child object of the column, not inferred only from the parent `Table`
branch and not preserved as a binary descriptor.

## Diff Classification Rule

Every new byte/list diff must be classified before code changes:

1. **Public object property** - visible in the palette, property panel, platform
   vocabulary, or stable object behavior. Add a named XSD property, dump/build
   mapping, semantic digest coverage, and a slot descriptor.
2. **Graph identity or relation** - object id, UUID, command source, event
   handler, attribute/type-domain link, table column editor, page ownership, or
   layout dependency. Add a named relation or identity field and validate the
   whole graph.
3. **Canonical writer generation detail** - root record kind, old top-stream
   length, marker, counter, slot count, default root panel shape. Keep this
   internal and emit the current canonical platform generation.
4. **Platform noise** - timestamps, save counters, regenerated UUIDs where the
   platform owns identity, and accepted platform canonicalization. Ignore in
   semantic digest and do not chase byte identity.
5. **Validation environment loss** - missing configuration metadata, absent
   type objects, or unloaded configuration support state. Fix the validation
   infobase, not the writer.
6. **Unknown residue** - do not expose it as XML. Keep the case failing in a
   coverage report until it is classified by platform evidence.

This rule is the missing guardrail. Without it, each strict-load or byte diff
tempts the implementation to add another public marker, and the next cleanup
removes that same marker again.

## Main Divergence From 1C

1C persists an object graph through a generic serializer. Our historical fixes
often treated a byte diff as proof that the original stream shape must be
preserved publicly. That created public `SerializationProfile`, `slotN`,
`dimensionProfile`, and tail fields.

Those fields are not the ordinary form object model. They are symptoms of
missing named concepts or missing internal canonical writer rules.

The correct rule is:

```text
read many platform generations; write one internally consistent canonical
generation; expose only named object-model properties.
```

If a low-level value is required for rebuild, it must be classified:

- real user-visible form/control property -> named XSD property;
- real identity or graph relation -> named `uuid`, `id`, event, binding, page,
  command source, column editor, etc.;
- writer generation/default/counter/noise -> internal descriptor rule or
  explicitly named non-editing metadata only when the platform exposes it as a
  stable concept;
- unknown stream residue -> not public XML and not a fallback; keep it as a
  failing coverage gap until the platform concept is identified.

## Mistakes That Caused The Loop

- Preserving old root/control profiles to chase byte identity before deciding
  whether the value was a platform object property.
- Accepting `SerializationProfile` and `slotN` as "typed enough"; they are
  renamed raw stream structure.
- Treating per-control strict-load success as a full graph success. It misses
  event/action tables, attribute links, table column editor controls, UUID
  identity, and type-domain references.
- Validating corpus forms in an empty or wrong infobase, which can produce
  false semantic loss because metadata object types are unavailable.
- Letting tests encode raw shape preservation. These tests prevent the writer
  from converging on a canonical current platform generation.
- Using coverage numbers for controls as if they were property-slot coverage.
  The audit still shows many public properties that are schema-only or
  descriptor-only.

## What To Delete

- Public form-level `SerializationProfile`, position `dimensionProfile`, and
  all public `slotN` fields.
- Public position shape attributes that only select internal geometry layouts.
  The first removed set is `layoutTail`, `layoutPreTail`, and
  `primaryDimensionMarker`.
- Root/panel layout tail/header/prefix attributes unless platform evidence
  proves a real object-model meaning. Candidates to remove or rename after
  classification are `dependencyTail`, `pageLayoutHeader`,
  `postLayoutTailBeforeColor`, `postLayoutTailAfterColor`, and dependency group
  `prefix`/`header`.
- Tests that require `RootRecord slot5..slot10`, `TopLevel slotN`, or other raw
  stream shape names in public XML. The same applies to tests that assert
  position tail strings instead of named layout semantics.
- Any new sidecar/profile/fallback mechanism whose only purpose is to keep an
  old `Form.bin` shape.
- Public names that say "profile" only because we do not know the platform
  property yet. Keep those as codec gaps, not as editable XML.

## What To Rewrite

- Replace `Form/SerializationProfile` with named concepts only:
  root layout/page state where it is a real form layout concept, stable object
  identity where the platform identity is meaningful, and canonical internal
  defaults for record generation fields.
- Make root record and top-level form stream writing canonical inside
  `ordinary_stream.py`; do not read record kind/title marker/top-level slots
  from public XML.
- Move the root panel handling to the same pattern as `PanelLayout`: named page
  state, page layouts, layout dependencies, base style, and no raw profile
  wrapper.
- Treat table column `ElementControl` as a nested typed control/editor concept.
  It can be an InputField, ChoiceField, etc.; do not infer it from only the
  parent table branch.
- Continue classifying position layout metadata such as `layoutMode`,
  `layoutGroup`, `layoutOrder`, `layoutNextOrder`, `layoutFlag1`, and
  `layoutFlag2`. Keep only real graph/layout concepts public; move pure stream
  selectors into internal descriptor profiles.
- Split `geometry_stream_from_xml` into a small public layout model reader and
  internal geometry descriptors. The descriptor should choose the platform
  record shape from named bindings, page ownership, parent size, data binding,
  and control kind, not from XML tail attributes.
- Move object-model XML construction out of `cli.py` into a model/dump module.
  `cli.py` should orchestrate commands; it should not be the place where raw
  geometry fragments become public XML.

## What To Add

- A semantic graph digest used in tests and corpus reports:
  controls by id/type/name/uuid, parent-child order, positions, bindings,
  attributes/type domains, events/actions, table columns/editor controls,
  pictures, fonts, colors, and command sources.
- A schema guard that fails on public raw-shape vocabulary:
  `SerializationProfile`, `TopLevel`, `RootRecord`, `slotN`, `Raw*`,
  `PlatformRecords`, `ListStream`, `ObjectModel`, `FormBin`, and equivalent
  renamed dump structures.
- Property-slot coverage, not only control coverage. The audit should report
  which XSD properties have dump and build mappings and which are only schema
  names.
- A list-diff triage tool that reports each mismatch path with the
  classification above: public property, graph relation, canonical writer
  detail, platform noise, validation environment loss, or unknown residue.
- A canonical writer matrix:
  build with current writer, strict-load in 8.5, strict-load in 8.2 where CLI
  supports it, platform redump, compare semantic graph digest.
- Corpus validation in an infobase with the relevant configuration loaded when
  forms contain metadata object types.
- Sanitized OACS checkpoints for every accepted platform conclusion, with
  evidence references attached to memories.

## Correct Next Steps

Done after this audit:

- form-level `SerializationProfile`, `RootRecord`, `TopLevel`, and public
  `slotN` schema fields were removed from `OrdinaryForm.xsd`, dump, build, and
  focused tests;
- root panel state/layout now uses the named `RootPanelLayout` XML node;
- root/top record generation is no longer read from public XML and is handled
  as canonical writer logic;
- position `dimensionProfile` is no longer public XML, and dimensions above
  the named height/minHeight/stretch/width set are emitted as
  `dimension="extra" extraIndex="N"` instead of `dimension="slotN"`;
- simple inline dimension segment counts and default primary dimension markers
  are now derived from `DimensionBinding` sections instead of being written as
  public marker attributes;
- `Position/@unit` is no longer public XML, and `Binding/@coordinate` no
  longer has a `slotN` fallback for the six named coordinate slots;
- `Position/@dimensionSegments` is no longer public XML; segmented dimension
  geometry is derived from the `DimensionBinding section` order by the writer.
- `Position/@secondaryDimensionMarker` is no longer public XML; the writer uses
  the canonical secondary marker for counted/dual dimension groups.
- schema tests now guard against reintroducing the removed public raw-shape
  vocabulary.
- semantic graph digest support was added as `digest-xml`; it hashes the
  normalized object model across controls, positions, bindings, events,
  attributes, table columns/editor controls, pictures, fonts, colors, and
  command sources while ignoring container timestamps and current codec-shaped
  position noise.
- `scan-corpus` can now attach semantic digest summaries to exported forms and
  compare matching forms across two exported trees before byte-level analysis.

Remaining next steps:

1. Classify the remaining `Position` layout metadata: `layoutMode`,
   `layoutGroup`, `layoutOrder`, `layoutNextOrder`, `layoutFlag1`, and
   `layoutFlag2`. `layoutGroup/layoutOrder` currently behave like graph/order
   relations; the flag fields still look like raw stream selectors.
2. Classify root/panel layout tail and group prefix/header values. Keep only
   real layout concepts public; move pure stream separators into internal
   descriptors.
3. Extend property-slot coverage so the audit reports dump/build mapping for
   each public XSD property, not only per-control writer coverage.
4. Re-run the small all-controls fixture and Diadoc fixture.
5. Re-run UT/Enterprise-style corpus checks only in a matching configured
   infobase to avoid type-loss noise.
6. Use the semantic graph digest as the default corpus success metric. Use
   byte identity only for current-generation platform-oracle fixtures and for
   localizing a strict-load failure.

The goal is not to make every old source byte-identical. The goal is to make
the public XML a complete editable object model and make the writer emit a
platform-readable canonical ordinary form stream without hidden raw data.
