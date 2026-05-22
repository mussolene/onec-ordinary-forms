# Research Map

This repository keeps research notes at the level of verified approach and
stable public/tooling references. Do not commit private EPF/ERF files, customer
exports, full platform dumps, license files, `nethasp.ini` contents, or host
paths.

## Publications and Prior Art

Use public write-ups and repositories as direction-finding, not as source of
truth for the serializer:

- Infostart article `2688294`: useful as a prior-art signal that ordinary form
  decomposition is feasible and that platform streams can be mapped into a
  more readable model. Treat details as hypotheses until confirmed on current
  exports.
- Rugut/UPP `v82.all/Описание.txt`: useful as an example of large 8.2-era
  unpacked metadata layout and naming conventions.
- PrettyData-style form decomposition references: useful to compare how others
  split form binary/XML/text, but this project should avoid a payload dump model
  and keep the editable representation object-oriented.
- `1c-help` MCP: useful for vocabulary and behavior of ordinary-form runtime
  objects such as `Панель`, `СтраницыПанели`, `ТабличноеПоле`, `ПолеВвода`,
  `ПолеКартинки`, `КоманднаяПанель`, `КнопкаКоманднойПанели`, and binding
  methods. It is not sufficient to define serialized stream structure.

All external findings need a local confirmation loop:

1. Export an EPF/ERF with platform `ibcmd`.
2. Decode the form stream into the object model.
3. Rebuild.
4. Re-export or load with platform tooling.
5. Compare stable XML and runtime behavior.

## Knowledge Transfer Filter

Useful knowledge to keep in this repository:

- ordinary-form publications and corpus examples that affect the object model;
- platform export/import behavior and exact CLI constraints;
- license/container launch requirements needed to run checks;
- secret scanning and leak wording rules;
- platform library names that guide future stream analysis;
- compact public image/version facts for reproducible runs.

Knowledge that should stay in broader infrastructure repositories unless it
becomes directly relevant here:

- PAI first-run onboarding and project bootstrap UX;
- PostgreSQL/server-cluster smoke details;
- complete technological-log analyzer implementation details;
- full release-history timelines for unrelated image versions;
- raw platform help dumps, binary string dumps, or disassembly output.

## Leak and Secret Scanning

Before every commit, scan staged content for:

- host paths: `/Users/`, `/home/<user>`, `/private/`, `/var/folders/`;
- OACS state: `.agent/oacs`, `.oacs`, `oacs.db`, key files;
- license data: `nethasp.ini` contents, license server addresses, license
  tokens;
- credentials: `.env`, passwords, passphrases, private keys, API tokens;
- private 1C artifacts: EPF/ERF, CF/DT, platform archives, exported customer
  metadata.

Recommended local check:

```bash
git diff --cached --check
git diff --cached --name-only
git diff --cached | rg -n -i '(/Users/|/home/[^/[:space:]]+|/private/|/var/folders/|password\s*=|passphrase\s*=|secret\s*=|token\s*=|private key|NH_SERVER_ADDR|NH_TCPIP|NHS_SERVER)'
gitleaks git --log-opts="HEAD..HEAD" --redact --no-banner
```

Documentation may legitimately mention placeholder names such as
`<local-nethasp.ini>` or container-internal paths. Do not weaken the scan for
real commits without understanding why a match is safe.

## Technological Log Leak Vocabulary

If this repository grows runtime validation around generated forms, use the same
careful wording as the 1C technological-log analyzer work:

- `MEM` positive net growth means release was not observed inside the analyzed
  log window; it is not proof of a leak by itself.
- `LEAKS` requires corresponding platform leak events.
- `SCRIPTCIRCREFS` is a separate circular-reference signal.
- Correlate by process/session/client/connect identifiers where available, and
  avoid over-attributing memory ownership to a single call without allocation
  correlation ids.

For ordinary-form work this matters when a rebuilt form opens/runs but appears
to leak after repeated open/close cycles: report the exact signal observed, not
a stronger conclusion.

## Validation Tooling Notes

Primary validation should use `ibcmd` because it is platform tooling and does
not require the source layout assumptions that `vrunner`/Vanessa use.

Useful fallback knowledge:

- The developer image includes Vanessa/OneScript tooling and can run
  `compileepf`/`decompileepf`; use that only when comparing with legacy EPF
  source layouts or when the task explicitly asks for Vanessa compatibility.
- When running simple CLI checks in the developer image, prefer an explicit
  `--entrypoint` such as `sh` or `oscript` to avoid unrelated VNC/XFCE service
  output.
- For old 8.2 Designer checks under Wine, the server-only Linux 8.2
  distribution is not enough because it lacks `1cv8`/`1cv8c`/`ibcmd`.
  The Wine-based image with Windows client binaries is the relevant runtime.
- `CREATEINFOBASE` and Designer `/CheckConfig` can work under Wine when
  licensing is mounted correctly, so a license failure should be treated as an
  environment/mount problem before assuming the configurator is absent.

Embedded knowledge packs in the developer image are useful for API lookup:
compressed `.kb.db.zst` packs are decompressed through the supported context
wrapper on first use. Do not directly depend on raw pack internals from this
package.

## Platform Libraries to Inspect

The primary source of truth is still platform export/import behavior, but the
platform binaries are useful for names, resource strings, and neighboring
concepts.

8.2.19.130 Wine image:

- `dsgnfrm.dll`: first candidate for designer ordinary-form serialization and
  form-editor behavior.
- `mngui.dll`, `mngbase.dll`, `mngcore.dll`, `mngdsgn.dll`: managed/designer
  metadata and form model glue.
- `frmcore` is not present as a separate 8.2 DLL in the observed image; use
  `dsgnfrm.dll` plus UI/metadata DLLs as the 8.2 starting point.
- `fmtd.dll`, `fmtdui.dll`, `pictedt.dll`, `image.dll`, `imageui.dll`: likely
  useful for geometry/text formatting and embedded pictures.
- `config.dll`, `pack.dll`, `xml2.dll`, `crcore.dll`, `core82.dll`, `wbase82.dll`:
  likely useful for stream/container/XML infrastructure.

8.5.1.1343 Linux image:

- `frmcore.so`: modern form core, useful for comparing concepts even when
  managed forms dominate.
- `mngui.so`, `mngbase.so`, `mngcore.so`, `mngdsgn.so`, `config.so`: metadata
  and designer/form model glue.
- `fmtd.so`, `fmtdcmn.so`, `fmtdui.so`, `pictedt.so`, `image.so`, `imageui.so`:
  formatting and picture handling.
- `pack.so`, `xml2.so`, `crcore.so`, `core85.so`, `wbase.so`: stream/container
  infrastructure.

Suggested inspection order:

1. Extract strings/symbol names from 8.2 `dsgnfrm.dll` and 8.5 `frmcore.so`.
2. Search for ordinary-form runtime object names and property names confirmed
   by `1c-help`.
3. Compare nearby strings with tags/fields produced by `ibcmd` export.
4. Use differences only as hypotheses until validated by a real export/rebuild
   round trip.

Do not commit binary excerpts, full string dumps, or disassembly output. Store
small sanitized findings in OACS memory only after verification.

## Reproducible Image Facts

Known useful image for this work:

- `ghcr.io/mussolene/1c-developer:8.5.1.1343`
- `docker.io/mussolene/1c-developer:8.5.1.1343`
- linux/amd64 manifest digest:
  `sha256:b67f7e3126864316bf1a716d87344700139567f232da9c58af61486f6f44a6b2`

Use tags for normal local work and the digest only when a check must be exactly
reproducible. Keep any future image publication notes compact: tag, platform,
digest, and the behavior verified for this repository.
