# Research Map

This repository keeps research notes at the level of verified approach,
sanitized tool behavior, and public references. Do not commit private
processors, customer exports, full platform dumps, disassembly output, license
configuration, OACS state, or host-specific paths.

## Direction

The ordinary form model is built from three evidence sources:

- platform export/import behavior;
- public documentation and prior art for ordinary form controls;
- small sanitized observations from platform libraries.

Public articles and repositories are useful for vocabulary and comparison, but
the serializer should be treated as correct only after a platform round trip
confirms it.

## Validation Loop

1. Export a private EPF/ERF with platform tooling into an ignored directory.
2. Decode `Ext/Form.bin` into the object XML package.
3. Rebuild the form stream from object XML.
4. Ask the platform to read or export the rebuilt processor.
5. Compare stable XML and runtime behavior.

For ordinary `Form.bin` writer work, Designer batch export is stricter than a
metadata-only load/check cycle and is the preferred acceptance check.

## Leak Scan Checklist

Before publication or commit, check current content and staged changes for:

- host paths and private mounted volumes;
- `.agent/oacs`, `.oacs`, local databases, key files, and `.env`;
- `nethasp.ini` contents, license server addresses, and license tokens;
- passwords, passphrases, private keys, API tokens, and GitHub tokens;
- EPF/ERF/CF/DT files, platform archives, generated dumps, and customer
  metadata.

Recommended local commands:

```bash
git status -sb
git diff --cached --check
git diff --cached --name-only
git diff --cached | rg -n -i '(/Users/|/home/[^/[:space:]]+|/private/|/var/folders/|password\s*=|passphrase\s*=|secret\s*=|token\s*=|private key|NH_SERVER_ADDR|NH_TCPIP|NHS_SERVER)'
gitleaks detect --source . --redact --no-banner
```

Documentation may mention placeholder values such as
`<local-nethasp.ini>`. Do not record real local values.

## Platform Areas

The platform binary scan has been useful for names and neighboring concepts,
not as a replacement for round-trip validation.

Known useful areas:

- ordinary form designer libraries for control and property vocabulary;
- UI/metadata libraries for shared form concepts;
- core stream/container libraries for `ListInStream` and `ListOutStream`;
- formatting and picture libraries for value serializers.

Keep raw binary strings, symbols, traces, and dumps in ignored local work
directories. Promote only compact verified conclusions into OACS.
