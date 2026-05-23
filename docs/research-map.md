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

Primary container validation should use platform tooling and should match the
layer being checked.

For ordinary `Form.bin` writer work, `ibcmd config load` plus
`ibcmd config check` is not strict enough: it can accept an EPF whose ordinary
form later fails with "Ошибка формата потока". The stricter platform check is:

```bash
. .agent/local/nethasp.env
tools/platform_validate_epf.sh /path/to/processor.epf
```

That helper runs Designer batch mode in the 8.5 amd64 container and executes
`/DumpExternalDataProcessorOrReportToFiles`. This command reads the external
processor and deserializes ordinary `Ext/Form.bin` deeply enough to reject a
malformed list stream.

The matching platform writer for external processor XML is
`/LoadExternalDataProcessorOrReportFromFiles`. A platform dump loaded with that
command and then dumped again should validate cleanly. This pair is useful as a
reference behavior check, while the repository still owns its own editable
ordinary-form XML model.

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

Observed with `LD_DEBUG=libs,files` during both
`/DumpExternalDataProcessorOrReportToFiles` and
`/LoadExternalDataProcessorOrReportFromFiles`, the important modules initialize
in the same order:

1. `core85.so`, `coreui85.so`
2. `xml2.so`, `xdto.so`, `pack.so`
3. `frmcore.so`, `dsgnfrm.so`, `dsgncmd.so`
4. `fmtd.so`, `fmtdcmn.so`, `bsl.so`
5. `mngcore.so`, `scheme.so`, `mngbase.so`, `mngui.so`, `mngdsgn.so`
6. `config.so`, `crcore.so`

Symbol and resource inspection confirms the split of responsibilities:

- `core85.so` defines `ListInStream`, `ListOutStream`, `GenericValue`,
  `TypeDomainPattern`, `CompositeID`, and typed serializers such as
  `Font`, `Color`, `V8Border`, `V8Line`, `ShortCut`, `LocalWString`.
- `mngui.so` and `config.so` import `ListInStream`/`ListOutStream` and the
  core typed serializers. They also contain many form/property resource names.
- `dsgnfrm` resources contain form designer dialogs, `controls.png`, and
  ordinary form designer property identifiers such as `IDS_PROPERTY_NAME`,
  `IDS_PROPERTY_TYPE`, `IDS_PROPERTY_LENGHT`, `IDS_PROPERTY_PRECISION`,
  `IDS_PROPERTY_DATE_TIME_MODE`, and `IDS_PROPERTY_FILL_CHECK`.
- `mngui` resources contain the managed/logical form vocabulary for shared
  form controls: `IDS_FIELDKIND_TEXT`, `IDS_FIELDKIND_INPUT`,
  `IDS_FIELDKIND_CHECKBOX`, `IDS_FIELDKIND_IMAGE`,
  `IDS_FIELDKIND_RADIOBUTTONS`, `IDS_FIELDKIND_MOXEL`,
  `IDS_FIELDKIND_TEXTDOC`, `IDS_FIELDKIND_CALENDAR`,
  `IDS_FIELDKIND_PROGRESSBAR`, `IDS_FIELDKIND_TRACKBAR`,
  `IDS_FIELDKIND_CHART`, `IDS_FIELDKIND_GANTTCHART`,
  `IDS_FIELDKIND_DENDROGRAM`, `IDS_GROUPKIND_*`, and `IDS_BUTTONREPRES_*`.
- `config` resources include XCF command/resource files such as `model.xdto`,
  `mobileApp.xsd`, `mobileForm.xsd`, and `xcf_dump_info.xsd`, but the platform
  XML export still keeps ordinary forms as `Ext/Form.bin`; there is no observed
  public ordinary-form object-model XML schema emitted by platform export.

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
