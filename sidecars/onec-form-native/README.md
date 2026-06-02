# onec-form-native

Native sidecar for ordinary-form codec work.

This directory is intentionally separate from the Python package. The first
tool is a clean-room list-stream reader/writer used for oracle comparisons and
future migration of stable codec pieces into a native component.

The sidecar must not expose raw list streams in public `Form.xml`. Its outputs
are diagnostic artifacts for ignored `scan-output/` runs and internal codec
tests only.

## Build

```bash
make -C sidecars/onec-form-native
```

The build uses the system C++ compiler and writes the binary to `build/oof-native`.

## Commands

Parse from stdin and print compact canonical bracket text:

```bash
sidecars/onec-form-native/build/oof-native compact < stream.txt
```

Parse from stdin and print platform-style `ListOutStream` text:

```bash
sidecars/onec-form-native/build/oof-native listout < stream.txt
```

Print structural statistics as JSON:

```bash
sidecars/onec-form-native/build/oof-native stats < stream.txt
```

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

Run the native ordinary-control transfer record codec check:

```bash
sidecars/onec-form-native/build/oof-native controls-codec
```

Run the internal ordinary form graph to transfer-set codec check:

```bash
sidecars/onec-form-native/build/oof-native graph-codec
```

Run the transfer-set write/read round-trip:

```bash
sidecars/onec-form-native/build/oof-native transfer-roundtrip
```

Print the native transfer sections with proven count/byte semantics:

```bash
sidecars/onec-form-native/build/oof-native transfer-sections
```

Dump the current native materialized form graph as public `OrdinaryFormV2`
managed-style XML:

```bash
sidecars/onec-form-native/build/oof-native formbin-dump-xml Form.bin Form.xml
sidecars/onec-form-native/build/oof-native runtime-form-dump-xml runtime-form-stream.txt Form.xml
```

Apply supported public XML edits back to a runtime stream or a real `Form.bin`
container:

```bash
sidecars/onec-form-native/build/oof-native runtime-form-build-xml base-runtime-stream.txt Form.xml rebuilt-runtime-stream.txt
sidecars/onec-form-native/build/oof-native formbin-build-xml base-Form.bin Form.xml rebuilt-Form.bin
```

The XML projection is native C++ and emits `Form`, `ChildItems`, `Attributes`,
`Commands`, `Events`, and named control elements. It does not expose raw
list-stream, payload, or fallback nodes. Use the coverage command before
treating a dump as rebuild-complete:

```bash
sidecars/onec-form-native/build/oof-native formbin-xml-coverage Form.bin
```

Current coverage is intentionally explicit: native XML projection is present,
and name plus simple localized `Title` edits round-trip through native runtime
and Form.bin build commands. Full XML-to-Form.bin writing still requires typed
`cf_form_controls8` payload properties for positions, attributes, commands,
events, and the remaining control-specific property slots.
