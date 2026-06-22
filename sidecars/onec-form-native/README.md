# onec-form-native

Native ordinary-form codec and CLI.

This directory owns the current implementation surface. The old package
was removed so ordinary-form parsing, package dump/build, object-model
inspection, and validation gates live in one native C++ tool.

The native tool must not expose raw list streams in public `Form.xml`.
Diagnostic outputs may inspect internal streams, but the release-facing source
package is `Form.xml`, `Form/Module.bsl`, and picture sidecars.

## Build

```bash
make -C sidecars/onec-form-native
```

The build uses the system C++ compiler and writes the binary to `build/oof-native`.

## Product commands

The release-facing CLI surface is the ordinary-form source package workflow:
dump a `Form.bin` to public `Form.xml`, build a `Form.bin` back from that
package, and run product gates/coverage checks.

Dump the current native materialized form graph as a public `OrdinaryForm`
managed-style package:

```bash
sidecars/onec-form-native/build/oof-native formbin-dump-package Form.bin Form.xml
```

Build a real `Form.bin` from the public source package through the object path:

```bash
sidecars/onec-form-native/build/oof-native formbin-build-source-package Form.xml rebuilt-Form.bin
```

Check current public XML projection coverage before treating a dump as
rebuild-complete:

```bash
sidecars/onec-form-native/build/oof-native formbin-xml-coverage Form.bin
```

Run the object-model safety gate. This is not release readiness while coverage
gaps remain:

```bash
sidecars/onec-form-native/build/oof-native object-model-gate
```

The package projection is native C++ and emits `Form`, `ChildItems`,
`Attributes`, `Commands`, `Events`, named control elements, and `Form/Module.bsl`.
It does not expose raw list-stream, payload, or fallback nodes.

## Diagnostic commands

Diagnostic commands are still dispatched for tests and investigation, but they
are not product build/dump equivalents. Run `oof-native` with no arguments for
the product surface, or set `OOF_NATIVE_SHOW_DIAGNOSTICS=1` to include the
diagnostic command list in usage output.

Print the platform-derived mechanism map embedded in the native sidecar:

```bash
sidecars/onec-form-native/build/oof-native mechanism
```

The mechanism output includes the platform-derived value surface, the
`core85.so` addresses for `LocalWString`/`FormattedString`, and the native
registry for `cf_form_controls8`, `cf_form_controls_position8`, and
`cf_form_controls_info8`. This sidecar records platform mechanism evidence and
internal codec structures only; it is not a public XML format.

At the proven transfer boundary, `cf_form_controls_position8` and
`cf_form_controls_info8` are fixed-record lists, while `cf_form_controls8` is a
raw payload file/HGLOBAL. The native diagnostic fixture named
`DiagnosticControlPayloadChunk` only exercises the transfer envelope; it is not
a platform-derived control record schema.

Run the native value stream round-trip for `FormattedString` and
`LocalWString`:

```bash
sidecars/onec-form-native/build/oof-native value-roundtrip
```

Current coverage is intentionally explicit: native XML projection is present,
and name, simple localized `Title`, basic `Position left/top/right/bottom`,
scalar `Binding coordinate/value`, typed anchor-list `Binding` records,
scalar `DimensionBinding dimension/value`, and typed dimension binding records
round-trip through native runtime and Form.bin build commands. Anchor records
stay platform-shaped internally but are projected as named XML anchors
(`From`, `To`, `Extra`) with readable `targetName` hints when the target object
is materialized.

Base-backed XML build commands are no longer public product commands. If a
test or investigation needs a baseline edit, keep it in ignored `scan-output/`
or an explicitly diagnostic command; do not document it as a product build path.
