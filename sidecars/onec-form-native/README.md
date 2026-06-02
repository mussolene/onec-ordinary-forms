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

Run the native value stream round-trip for `FormattedString` and
`LocalWString`:

```bash
sidecars/onec-form-native/build/oof-native value-roundtrip
```

Run the native ordinary-control transfer record codec check:

```bash
sidecars/onec-form-native/build/oof-native controls-codec
```
