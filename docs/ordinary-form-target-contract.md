# Ordinary Form Contract

This repository has no external consumers to protect. Breaking old command
surfaces, XML shapes, helper scripts, compatibility aliases, and fallback paths
is allowed when it makes the product architecture cleaner.

There is one product: a platform-like `OrdinaryForm` object graph.

There is one release path:

```text
Form.bin -> ListInStream -> OrdinaryForm -> Form.xml + Module.bsl + Items/*
Form.xml + Module.bsl + Items/* -> OrdinaryForm -> ListOutStream -> Form.bin
```

Everything outside that path is research, diagnostics, or temporary scaffolding.

## Product Model

`OrdinaryForm` owns the form, controls, nested controls, attributes, commands,
events, positions, bindings, identities, typed values, defaults, module text,
picture references, and platform-like `GetPropVal` / `SetPropVal` behavior.

`Form.bin` is not the model. It is only a container for the serialized form
stream and module stream.

Public `Form.xml` is not a dump format. It is the editable projection of
`OrdinaryForm`.

## Concept Registry

The only owner of public ordinary-form model decisions is the executable
`OrdinaryFormConceptRegistry`.

Platform schemas, help/API extracts, runtime bindings, descriptor joins, oracle
streams, corpus checks, and Designer validation are evidence adapters. They do
not decide public XML concepts on their own and they do not form a trust
hierarchy.

Storage names are evidence, not public names. API names are evidence, not
storage slots. Runtime types are evidence, not public object names. Negative
search results in a help index are evidence about that index only.

Every accepted public concept must have one registry row:

```text
API/help name -> public XML name -> runtime identity -> storage member/codec -> proof
```

Example:

```text
ПолеВвода / Поле ввода -> InputField
-> 381ed624-9217-4e63-85db-c4c3cb87daae
-> TextBox / txt
-> oracle/corpus proof
```

`TextBox` cannot be mapped by type alone because `InputField`, `ChoiceField`,
and `ListBox` share that storage shape. The registry row, not the storage type,
owns that distinction.

## Add Concepts This Way

When XML cannot rebuild a value, add the missing named object-model concept.
Do not patch old payloads.

A new concept is accepted only after the registry declares:

- API/help name and type, or why it is storage-only;
- public XML name and schema owner;
- runtime identity: GUID, collection identity, event/command identity, or
  value-object identity;
- storage member, descriptor family, slot/default/write rule, or value codec;
- proof command or corpus/oracle validation.

If this row is incomplete, the concept stays diagnostic.

## Delete These, Do Not Preserve Them

Remove release-facing code or docs that require:

- source `Form.bin` as build input;
- baseline diffs;
- payload patching;
- raw/list-stream preservation;
- hidden raw object models;
- fallback binary blobs;
- compatibility profiles;
- old command aliases whose only purpose is not breaking old behavior.

Temporary diagnostics are fine under `scan-output/`, but they must not become
release inputs or public XML.

## Public Package

`Page` is identified in public XML by its name and owning `ChildItems` tree.
Its typed internal ID is allocated during parsing and is not written to XML.
Control IDs remain platform IDs. A public `Page.id` is rejected rather than
accepted through a compatibility branch. Current XML identity verification:
`ev_8340649070f5401787475122583e122c`.

Page `Visible` and `Enabled` are independent Boolean properties with default
`true`. XML omits default values and writes `false` explicitly. The schema
permits each property once, between `Title` and `ChildItems`. Verification:
`ev_df9462158f2a4c42a53b31741b9c99fb`. These XML properties do not imply that
the Panel/Page binary codec is implemented.

Page has an optional named `Position`, using the existing rectangle and
`Bindings` vocabulary. Absence remains implicit; an explicit position survives
XML serialization. The internal boundary codec currently supports fixed
Left/Top and explicit Right/Bottom constraints to the owning Form or Panel.
Width and Height are differences between endpoint coordinates. Missing owner
constraints, other targets, proportional constraints and unsupported flags
are rejected instead of reconstructed from a baseline stream.

Changing the observed Right constraint changes the runtime width of a bound
child while its saved rectangle remains fixed. This establishes a layout
effect, not the complete formula for every page variant. A subsequent Bottom
experiment confirms a four-pixel height change for a four-unit constraint
change, with the saved rectangle fixed. A larger change encounters another
size limit whose formula remains unproven. The root writer
now uses this named boundary encoder. Verification:
`ev_d8864750203a47bfa4413ae13c294c97`,
`ev_d1df9f1b84274f029da48a13d8776302`,
`ev_e2fddecde6c9498c8f119fd3d812cc7b`.
Whole recursive Panel/Page document serialization remains incomplete.

The ordinary-control geometry codec uses an explicit owner, page index and
local sibling ordinal. Within a Panel, platform target zero resolves to that
Panel in named bindings, including proportional targets. At root it resolves
to Form. A nested binding to Form is rejected until its separate storage
representation is established. Page and ordinal mismatches, invalid owners,
incoming IDs and source edges fail explicitly. This codec does not by itself
implement the recursive Panel/Page tables. Runtime Bottom evidence:
`ev_7efcc3e0e5ae4378b70db7f71010f4fc`.

The editable package is:

```text
Forms/<FormName>/Ext/Form.xml
Forms/<FormName>/Ext/Form/Module.bsl
Forms/<FormName>/Ext/Form/Items/<ElementName>/Picture.*
```

Everything in this package must describe named `OrdinaryForm` concepts. If a
low-level platform value is needed for rebuild, promote it into a named concept
with a descriptor-backed serializer.


The Page table codec now handles named Name, localized Title, Visible and
Enabled properties in both directions, preserving order and validating
identity, counts and language uniqueness. Position and children remain separate
owner surfaces. Unsupported property variations are rejected. The root writer
uses this codec for its standard page. Current macOS and fresh Linux suites
pass 11/11, and strict Designer root reconstruction from XML and module passes:
`ev_de0a60c9770b4bfa911e743a39c8acab`,
`ev_0c9e829731ff45df8ec194f0d5464c58`.

A controlled Designer experiment identifies a tab-picture descriptor within
Page metadata. Clearing a standard picture removes its UUID and changes Bottom
constraints for all three pages, without changing saved rectangles or child
geometry. The complete descriptor is not implemented; such pages fail
explicitly. Evidence: `ev_1c4a7ce42e2d453e9479eaafa15bdad8`.
Whole recursive Panel/Page document serialization remains incomplete.
