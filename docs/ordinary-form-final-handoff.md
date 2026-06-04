# Ordinary Form Final Handoff

This document is the durable handoff for the ordinary-form work. It exists to
stop repeating the same investigations and to keep the implementation aligned
with the target product: an ordinary 1C form stored in Git as a managed-form
style object model, editable by people and machines, and rebuildable without a
source `Form.bin` baseline.

## Acceptance Criteria For This Handoff

- AC1: Record current repository, OACS, documentation, and implementation state.
- AC2: Preserve the historical platform findings and explain why they matter.
- AC3: Separate proven product architecture from temporary compatibility paths.
- AC4: State whether the current implementation is viable and what is missing.
- AC5: Provide a machine-actionable prompt for finishing the target.

## Target Product

The target public source representation is:

```text
Forms/<FormName>/Ext/Form.xml
Forms/<FormName>/Ext/Form/Module.bsl
Forms/<FormName>/Ext/Form/Items/<ElementName>/Picture.gif
```
`Form.xml` is the public editable object model. It must contain named platform
concepts only: `Form`, `Attributes`, `Commands`, `Events`, `ChildItems`,
`Page`, `Panel`, `Button`, `InputField`, `Table`, `CommandBar`,
`LabelDecoration`, `PictureDecoration`, `Position`, `Bindings`, typed values,
and platform-derived control properties. It must not expose raw stream shape,
indexed slots, binary fallbacks, or renamed dump structures.

The target internal pipeline is symmetric:

```text
Form.bin
  -> container file "form"
  -> platform bracket/list-stream
  -> PlatformFormObject
  -> XSD-backed public Form.xml

public Form.xml
  -> PlatformFormObject
  -> canonical platform bracket/list-stream
  -> container file "form"
  -> Form.bin
```

`Module.bsl` and picture files are package members, but the core release
criterion is the form object graph. `Form.bin` is an input/output container, not
a repository source file and not a baseline fallback.

## Historical Work Summary

The repository went through four broad phases.

1. Python container/list-stream prototypes.
   The project first learned to parse and pack ordinary `Form.bin` containers,
   read bracket/list-stream payloads, emit structural XML, and rebuild selected
   streams. This proved that the form payload is a parseable platform
   list-stream, but it also created the first temptation to expose raw
   `ListStream`, `SerializationProfile`, `slotN`, and profile/tail data.

2. Public XML cleanup.
   Raw and profile-like public fields were repeatedly removed or reclassified.
   The stable rule became: if a value is required for rebuild, promote it into a
   named platform concept or keep it as an internal codec gap. Public XML may
   not contain `ObjectModel`, `ListStream`, `BracketStream`, `FormBin`,
   `LogicalStream`, `RawBracket`, `PlatformRecords`, `SerializationProfile`,
   `slotN`, embedded base64 source streams, or equivalent renamed fallbacks.

3. Platform mechanism extraction and descriptor model.
   Ghidra, xref scans, library string/resource scans, platform XSD extraction,
   help catalog extraction, LD audit/LD debug, and runtime oracles were used to
   identify the actual platform mechanism. The result is the descriptor-backed
   model currently in native C++: list-stream codec, typed value serializers,
   platform object schema, property registry, runtime binding catalog, and
   control descriptor joins.

4. Native sidecar consolidation and direct writer restoration.
   The native C++ sidecar became the package dump/projection backend and can
   apply supported edits when `--base-bin` is explicitly supplied. The restored
   release baseline uses the existing Python direct writer for source-package
   rebuilds without a source `Form.bin`. Native C++ remains a deliberate porting
   path for that proven behavior, not a replacement by seed/template logic.

## Platform Reverse Engineering Findings

The ordinary-form persistence mechanism is not DFM and is not managed-form XML.
It is a platform list-stream object graph.

The important platform findings are:

- `dsgnfrm.so` is the ordinary-form persistence center.
- Write-side persistence constructs `core::ListOutStream`.
- Read-side persistence constructs `core::ListInStream`.
- The ordinary control payload families are identified by:
  - `cf_form_controls8 = 0x2500`;
  - `cf_form_controls_position8 = 0x5500`;
  - `cf_form_controls_info8 = 0x9d00`.
- Decompile/xref evidence identified entry points around:
  - `FUN_00255f70`: ListOutStream/write persistence entry;
  - `FUN_00256510`: ListInStream/read persistence entry;
  - `FUN_002709e0`, `FUN_00270da0`, `FUN_00270fe0`: ordinary control transfer
    triplet;
  - `FUN_002c9430`: info-only ordinary control entry.
- Transfer boundaries are not all fixed-record arrays:
  - `cf_form_controls_position8`: count plus 32-byte records;
  - `cf_form_controls_info8`: count plus 16-byte records;
  - `cf_form_controls8`: existing payload file/HGLOBAL at the GetData boundary,
    not a fixed 40-byte record family.
- `core85.so` serializer evidence was used for value objects:
  `LocalWString`, `FormattedString`, `CompositeID`, `TypeDomainPattern`,
  `GenericValue`, `Color`, `Font`, `V8Border`, `V8Picture`, `ShortCut`, `Date`,
  and `Numeric`.
- `mngcore_root.res`/XSD resources provide logform schema vocabulary:
  controls, commands, attributes, events, value schemas, layout members, and
  per-control public properties.
- `shcntx_root.hbk`/localized help catalogs provide runtime object/member names:
  `Form`, `FormItems`, `FormAttributes`, `FormAttribute`, `FormCommands`,
  `FormCommand`, `FormEvents`, `FormEvent`, `Button`, `TableBox`, `Panel`, etc.
- `frntend`, `mngbase`, `mngdsgn`, and `mngcore` contain compare/merge and
  `LogForm` runtime/render concepts, but current evidence does not prove that
  their compare/merge path serializes ordinary `Form.bin`. They are useful as
  vocabulary evidence, not as the ordinary-form writer path.

The implementation consequence is fixed:

```text
Read many platform profile generations.
Materialize one typed PlatformFormObject graph.
Write one canonical internally consistent ListOutStream generation.
Expose only named object-model properties in Form.xml.
```

## Repository Evidence And Durable Memory

Current key docs:

- `README.md`: product target, public XML boundary, current compatibility
  status.
- `docs/architecture.md`: layer split and platform codec formula.
- `docs/ordinary-form-pattern-audit.md`: anti-loop architectural conclusion,
  diff classification rule, and forbidden raw/profile patterns.
- `docs/platform-mechanism-extraction.md`: platform disassembly/extraction
  procedure and sanitized mechanism map.
- `docs/native-component-plan.md`: native sidecar, platform oracle, and phase
  plan.

Current relevant OACS context/evidence:

- `ctx_90e44bec70d048cca3c506eea685fca4`: context capsule for this historical
  analysis.
- `mem_d0b9e35322d24ee383b00c6834abe17f` with
  `ev_c798703a4a0f454cbad2066f8f1a8f17`: native `Form.xml ->
  PlatformFormObject` boundary exists; `--base-bin` is compatibility only.
- `mem_5de7b3cedc8d4f06b275e16d1f667f6d` with
  `ev_5dbc767aa4f544758aaa16f8bf70c0a9`: current platform mechanism map.
- `mem_27476e36c4ae42ea9a2842ee2cfff9ed`: split persistence layer from
  runtime/render layer.
- `mem_5052771cda3b4101a9366b7523446352`: correction for `cf_form_controls8`
  GetData handling.
- `mem_6fc36412814544418365aca3de707fb2`: adding fallback/profile patches is
  unstable; missing typed fields must become descriptor-backed concepts or
  coverage gaps.
- `mem_fa01d53fabe549278ee1273458f67df7`: Diadoc internal bracket/object
  roundtrip passed through object layer, proving the object boundary for
  no-change materialize/dematerialize.
- `mem_215b22599b5d4242910a7d9128eee675`: UT gate passed native no-op and
  set/get mutations, but public XML mutation still exposed binding/control-type
  gaps.

Do not rely on chat history as durable state. Query OACS and current files.

## Current Implementation Map

Native C++ sidecar:

- `sidecars/onec-form-native/src/platform_list_stream.hpp`
  implements platform-like `ListInStream`/`ListOutStream`.
- `sidecars/onec-form-native/src/platform_value.hpp` implements typed platform
  values and their list-stream serialization.
- `sidecars/onec-form-native/src/form_bin_container.hpp` parses/serializes the
  `Form.bin` container.
- `sidecars/onec-form-native/src/platform_mechanism.hpp` records sanitized
  platform mechanism constants and roles.
- `sidecars/onec-form-native/src/platform_form_schema.hpp` contains
  platform-resource-derived ordinary control schema facts.
- `sidecars/onec-form-native/src/platform_object_schema.hpp` builds the
  platform object schema surface.
- `sidecars/onec-form-native/src/platform_property_registry.hpp` maps public
  properties to slot codecs and evidence.
- `sidecars/onec-form-native/src/platform_runtime_binding.hpp` records runtime
  object/member evidence.
- `sidecars/onec-form-native/src/platform_object_model.hpp` defines
  `PlatformFormObject`, object collections, properties, and `get_prop_val` /
  `set_prop_val`.
- `sidecars/onec-form-native/src/main.cpp` currently owns materialization from
  bracket payload to `PlatformFormObject`, public XML writer, public XML parser
  back to `PlatformFormObject`, supported baseline edit application, and
  selftests.

Python layer:

- `src/onec_ordinary_forms/formbin.py`: container parser/writer.
- `src/onec_ordinary_forms/bracket.py` and `liststream.py`: list-stream parsing.
- `src/onec_ordinary_forms/ordinary_platform_graph.py`: Python platform graph
  prototype.
- `src/onec_ordinary_forms/ordinary_platform_object.py`: Python
  `PlatformFormObject` facade over the graph.
- `src/onec_ordinary_forms/ordinary_platform_xml.py`: internal object XML
  transfer view.
- `src/onec_ordinary_forms/ordinary_stream.py`: current proven direct writer
  from public `Form.xml` to the internal platform list-stream payload.
- `src/onec_ordinary_forms/native_bridge.py`: Python CLI bridge to native C++.
- `src/onec_ordinary_forms/cli.py`: CLI entrypoint. `build-bin` without
  `--base-bin` must dispatch to the direct source writer; `--base-bin` explicitly
  selects the native compatibility patch path.

Gates and tooling:

- `tools/release_gate.py`: public release gate over pytest, native selftests,
  public contract probes, and codec coverage.
- `tools/object_model_gate.py`: anti-loop guard; rejects writable unproven
  value codecs and raw-shape vocabulary.
- `tools/lossless_roundtrip_gate.py`: internal payload-lossless no-change gate.
- `tools/audit_codec_coverage.py`: property/control mapping coverage.
- `tools/platform_validate_epf.sh`: strict Designer validation where platform
  is available.
- `tools/ghidra_decompile_form_functions.sh`, `tools/ghidra_extract_form_xrefs.sh`,
  `tools/extract_platform_mechanism.py`: platform mechanism extraction.
- `tools/extract_platform_xml_resources.py`,
  `tools/vendor_platform_schemas.py`: platform XSD/resource extraction.
- `tools/platform_oracle_execute.sh`,
  `tools/platform_topdown_oracle.sh`,
  `tools/platform_property_proof.sh`,
  `tools/native_property_slot_proof.sh`: runtime/property oracle workflows.

## What Works Now

Verified current state before this handoff:

- `git status --short`: clean before writing this document.
- This document was refreshed after `df86305 Restore direct source build path`.
- Current release gate at that refresh passed:
  focused source-build tests, `make smoke`, and `python3 tools/release_gate.py`.
- Native package dump writes public `Form.xml`, `Form/Module.bsl`, and existing
  picture sidecars.
- Native package build compatibility path can apply supported edits to an
  original `Form.bin` graph passed as `--base-bin`.
- Native supports property set/get for proven codecs:
  name/title, scalar flags, position, bindings, attributes, commands, events,
  and leaf control deletion.
- Native public XML can now be parsed back to `PlatformFormObject`.
- The Python direct writer can rebuild `Form.bin` from public `Form.xml`,
  `Form/Module.bsl`, and sidecars without a source `Form.bin` baseline.
- Object-model gates reject raw public vocabulary and unproven writable value
  properties.
- Release gate reports full platform property mapping coverage, but this means
  descriptor/public mapping coverage, not complete writer slot semantics for
  every value property.

## What Is Not Finished

The release target is not blocked on a mandatory native writer. The current
working baseline already does this without a source `Form.bin` baseline through
the Python direct writer:

```text
public Form.xml
  -> object writer
  -> canonical ListOutStream payload
  -> Form.bin container
```

The current `formbin-build-package base-Form.bin Form.xml rebuilt-Form.bin`
path is a compatibility path. It preserves baseline payload shape and applies
supported named edits. It is not the final source build architecture.

Known missing or risky parts:

- The direct writer must be hardened against real UT/Diadoc corpus coverage and
  strict Designer validation before release readiness can be claimed.
- Native C++ does not yet own the full direct writer. That is an optional porting
  and hardening path, not the current release baseline.
- New controls from public `ChildItems` and new picture payload slots need
  explicit coverage tests for the direct source writer.
- Value-layer codecs for color/font/picture/border are readable and
  schema-backed, but not writable until info8 object-property semantics are
  proven by oracle.
- Binding extraction must stay control/descriptor-specific; generic geometry
  scanning caused false binding interpretations on Spreadsheet payloads.
- Strict Designer validation requires the right platform/license/infobase
  environment; metadata-only load is not enough to prove an ordinary form
  stream opens correctly.

## Why The Previous Loops Happened

The project repeatedly treated byte/list-stream diffs as proof that old stream
shape had to be preserved publicly. That created temporary public concepts such
as profiles, tails, `slotN`, layout markers, and raw tree shapes. Later
platform evidence showed those were not user-facing object-model concepts, so
they were removed. This consumed time without moving the final architecture.

The correct rule is:

1. Classify each mismatch first.
2. If it is a public form/control property, add a named schema-backed property.
3. If it is identity/relation, add a named object graph relation.
4. If it is writer generation detail, keep it internal and canonical.
5. If it is platform noise, ignore it in semantic digest.
6. If it is validation environment loss, fix the validation setup.
7. If it is unknown residue, keep it as a coverage gap and do not expose it.

Passing Designer validation is required but not sufficient. A public XML dump
that is only renamed raw stream shape is architecturally wrong even if Designer
accepts the rebuilt binary.

## Viability Conclusion

The implementation direction is viable.

The repository already has the necessary algorithmic pieces:

- list-stream parser/writer;
- `Form.bin` container parser/writer;
- platform-derived control and property descriptors;
- value serializers derived from platform evidence;
- platform runtime/object schema catalogs;
- Python direct source writer;
- native `PlatformFormObject` model;
- native `Form.bin -> PlatformFormObject -> Form.xml`;
- native `Form.xml -> PlatformFormObject`;
- gates that prevent raw public fallback and unproven writable properties.

The remaining problem is not research direction. It is cleanup, coverage, and
hardening around one release line: public source package -> direct writer ->
`Form.bin`. Any `--base-bin` path remains a compatibility shortcut and must not
be expanded into the target architecture.

## Required Next Implementation Plan

1. Freeze public XML vocabulary.
   Do not add raw/profile/fallback vocabulary. Use `object_model_gate.py` and
   schema validation as mandatory gates.

2. Stabilize the current direct writer baseline.
   Treat `ordinary_stream.form_stream_from_object_xml` and
   `formbin.build_form_bin_container` as the current working source-build
   implementation. Expand tests for root form, top-level controls, `Button`,
   `InputField`, `Panel`/`Page`, `Attributes`, `Commands`, `Events`,
   `Position`, bindings, module sidecars, and picture sidecars.

3. Keep native C++ as a deliberate porting path.
   If the codec is moved to C++, port the proven Python writer behavior into
   descriptor/object writer code. Do not replace it with seed templates,
   profile preservation, raw fallback, or a mandatory baseline.

4. Keep the source-build command/gate no-base by default.
   `build-bin --xml ... --out-bin ...` must build without `--base-bin`.
   Unsupported required concepts must fail fast with typed coverage diagnostics
   instead of leaking raw data into public XML.

5. Compare by semantic graph first, byte identity second.
   No-change source build should pass:
   `Form.bin -> XML -> direct writer -> Form.bin -> XML`, with stable semantic
   graph digest. Byte identity is a useful oracle for controlled cases only.

6. Validate on increasingly real corpora.
   Use small fixtures, all-controls fixture, Diadoc corpus, UT corpus, and
   strict Designer `/DumpExternalDataProcessorOrReportToFiles` where platform
   prerequisites are available. Record every result in OACS.

## Machine Prompt For Finishing The Goal

Use this prompt verbatim for the next implementation agent:

```text
You are working in the repository root.

Goal:
Complete and harden the ordinary-form source build architecture:
Form.bin -> public Form.xml source package and public Form.xml source package ->
canonical ListOutStream -> Form.bin, without requiring a source Form.bin
baseline for the target build command.

Mandatory context:
- Read AGENTS.md and follow OACS exactly.
- Query OACS before implementation:
  export OACS_DB="$PWD/.agent/oacs/oacs.db"
  acs memory query --query "ordinary form direct source writer no baseline build-bin Form.xml Module.bsl sidecars" --scope project --json
  acs context build --intent "finish and harden ordinary form no-base source build" --scope project --json
- Read docs/ordinary-form-final-handoff.md, docs/ordinary-form-pattern-audit.md,
  docs/platform-mechanism-extraction.md, docs/architecture.md, README.md.

Hard constraints:
- Do not expand --base-bin as the target architecture.
- Do not add raw/profile/fallback public XML: no ObjectModel, ListStream,
  BracketStream, FormBin, LogicalStream, RawBracket, PlatformRecords,
  SerializationProfile, slotN, embedded source base64, or renamed stream dumps.
- If a value is needed for rebuild, classify it as public property, graph
  identity/relation, canonical writer detail, platform noise, validation loss,
  or unknown coverage gap. Unknown gaps must fail with diagnostics, not leak
  into public XML.
- Use the public source package as the central release contract. Native C++ may
  mirror this object model, but it must not replace no-base source build with a
  seed/template/baseline-preservation path.

Acceptance criteria:
- AC1: OACS evidence and checkpoint exist for the iteration.
- AC2: Preserve and harden build-bin without --base-bin for a focused vertical
  slice: root form, child items, Button, InputField, Panel/Page, Position,
  bindings, Attributes, Commands, Events, Module.bsl, and picture sidecars where
  supported.
- AC3: Add tests proving XML -> direct writer -> Form.bin -> XML semantic
  stability without --base-bin.
- AC4: Add or update a command/gate for source build without source Form.bin
  baseline. Unsupported required fields must produce typed coverage diagnostics.
- AC5: Existing gates pass:
  make -C sidecars/onec-form-native test
  make test
  make smoke
  python3 tools/release_gate.py
- AC6: No public XML raw-shape vocabulary is introduced; object_model_gate.py
  remains PASS.
- AC7: Commit focused changes only after checks pass. Leave private corpora,
  scan-output, OACS DB/key files, platform archives, and generated local
  artifacts unstaged.

Implementation hints:
- Start from src/onec_ordinary_forms/ordinary_stream.py,
  ordinary_platform_graph.py, ordinary_platform_object.py,
  ordinary_platform_xml.py, and formbin.py.
- If porting to C++, mirror the proven behavior from those modules through
  sidecars/onec-form-native/src/platform_object_model.hpp,
  platform_list_stream.hpp, platform_value.hpp, platform_property_registry.hpp,
  platform_form_schema.hpp, and platform_object_schema.hpp.
- The source writer does not need to support every rare control in one
  iteration. It must fail explicitly on unsupported required concepts and must
  never silently preserve a baseline.

Stop condition:
If source-package -> ListOutStream cannot be completed for the selected scope in
the current iteration, record PARTIAL in OACS with exact missing writer concepts
and the first failing selftest. Do not claim release completion.
```
