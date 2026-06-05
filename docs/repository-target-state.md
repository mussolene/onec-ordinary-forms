# Repository Target State

This is the current target state for the repository.

The product path is exactly one object path:

```text
Form.bin/bracket -> ListInStream -> OrdinaryFormObject -> ListOutStream -> Form.bin/bracket
Form.bin/bracket -> OrdinaryFormObject -> Form.xml
Form.xml -> OrdinaryFormObject -> Form.bin/bracket
```

`OrdinaryFormObject` is the product. It owns the form graph, object identity,
properties, defaults, collections, events, bindings, positions, typed values,
module text, and picture references.

## No Release Fallbacks

Do not keep release-facing compatibility surfaces for:

- build paths that require an existing source `Form.bin`;
- payload patching;
- sidecars used to preserve unknown stream state;
- public raw/list-stream XML;
- hidden object dumps in public XML;
- baseline diff workers;
- old command aliases whose only purpose is compatibility.

If a value cannot round-trip through `OrdinaryFormObject`, the missing named
object/property/value descriptor must be added to the object model and its
descriptor-backed serializer.

## Allowed Diagnostics

Platform GUI, `LD_PRELOAD`, `LD_AUDIT`, strict Designer dump, corpus reports,
and local `scan-output/` files are oracle/evidence tools only. They are not
product architecture and must not appear as release build inputs.

## Current Migration Rule

When editing the codebase, prefer deleting or disabling legacy release-looking
commands over preserving compatibility. There are no external consumers to
protect. A command may remain only if it is on the object path above or is
clearly named and documented as diagnostic evidence.
