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

The editable package is:

```text
Forms/<FormName>/Ext/Form.xml
Forms/<FormName>/Ext/Form/Module.bsl
Forms/<FormName>/Ext/Form/Items/<ElementName>/Picture.*
```

Everything in this package must describe named `OrdinaryForm` concepts. If a
low-level platform value is needed for rebuild, promote it into a named concept
with a descriptor-backed serializer.
