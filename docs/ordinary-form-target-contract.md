# Ordinary Form Target Contract

This file is the project stop sign for ordinary-form work. If an approach
conflicts with this contract, it is a diagnostic or research path, not the
product architecture.

See also [`repository-target-state.md`](repository-target-state.md). That file
records the current migration target: one release path through
`OrdinaryFormObject`, with no compatibility aliases or release-facing fallback
builders.

## Product Object

The product object is `OrdinaryForm`: a platform-like mutable object graph.

`OrdinaryForm` owns:

- `Form` properties;
- `Attributes`;
- `Commands`;
- `Events`;
- `ChildItems`;
- ordinary controls and nested controls;
- typed values such as positions, bindings, pictures, colors, fonts,
  `CompositeID`, type descriptors, and platform defaults;
- `GetPropVal` and `SetPropVal` behavior for default and explicit values.

The object graph can be materialized from platform bracket/list-stream and from
public XML. It can be dematerialized back to both surfaces:

```text
bracket/list-stream -> OrdinaryForm -> Form.xml
Form.xml            -> OrdinaryForm -> bracket/list-stream
```

`Form.bin` is not the object. It is only a container for the serialized form
stream and module stream.

## Public Source Package

The public editable source package is:

```text
Forms/<FormName>/Ext/Form.xml
Forms/<FormName>/Ext/Form/Module.bsl
Forms/<FormName>/Ext/Form/Items/<ElementName>/Picture.*
```

`Form.xml` is the only editable form model. `Module.bsl` and `Items/...`
picture files are object properties stored as files because that is the
managed-form-style source layout. They are not a separate patch protocol.

## Serializer Layers

There are only two serializer layers:

- object serializer: `OrdinaryForm <-> Form.xml`;
- platform serializer: `OrdinaryForm <-> bracket/list-stream`;
- container packer: `form stream + module stream -> Form.bin` and the reverse.

Container pack/unpack must stay a thin boundary. It must not become a form
patcher.

## Hard No

Do not make any of these part of the product path:

- an existing source `Form.bin` as required build input;
- baseline diff as source-build logic;
- patch workers;
- raw/list-stream preservation fields;
- hidden raw object models in public XML;
- fallback binary blobs;
- synthetic canvas/state surfaces;
- compatibility profiles whose purpose is to keep an old `Form.bin` shape.

Commands may read an existing `Form.bin` to prove the object path
`ListInStream -> OrdinaryFormObject -> ListOutStream`, or write a new
`Form.bin` from that object after `SetPropVal`. They must not use an existing
`Form.bin` as hidden source state for `Form.xml` rebuild.

## Required Direction

When something cannot be rebuilt from XML, add the missing named concept to
`OrdinaryForm` and its descriptor-backed serializer. Do not patch the old
payload. The right fix is always one of:

- a named control;
- a named property;
- a named event or command;
- a typed value object;
- a default-value rule;
- a descriptor-backed slot codec.

The release path is:

```text
Form.bin -> OrdinaryForm -> Form.xml + Module.bsl + Items/*
Form.xml + Module.bsl + Items/* -> OrdinaryForm -> bracket/list-stream -> Form.bin
```
