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

Parse a flat platform `TypeDomainPattern` list and show the typed items plus
the exact list-stream roundtrip atoms:

```bash
printf '{"S","B"}' | sidecars/onec-form-native/build/oof-native type-domain
```

Parse the confirmed scalar subset of `ValueFromStringInternal`/
`ValueToStringInternal`:

```bash
printf '"Caption"' | sidecars/onec-form-native/build/oof-native value
```

Parse a localized string record in the observed ordinary-form shape
`{version,count,{lang,text}}`:

```bash
printf '{1,1,{"ru","Caption"}}' | sidecars/onec-form-native/build/oof-native localized
```
