# Native Ordinary-Form Component Plan

This plan records the sidecar direction for ordinary-form codec work. It keeps
the current Python parser and public XML contract stable while the native
component is built and validated in parallel.

## Scope

Acceptance criteria for the first native iteration:

- AC1: Do not change the current Python parser/writer behavior.
- AC2: Add a buildable native sidecar next to the Python package.
- AC3: Define the runtime-oracle comparison loop through the 1C platform.
- AC4: Keep raw platform/list-stream data out of public `Form.xml`.
- AC5: Record OACS evidence and an iteration checkpoint.

## Architecture

The product should have three boundaries:

- Public boundary: `Form.xml`, `Module.bsl`, and sidecar files. This remains
  the editable Git representation and must contain named object-model concepts,
  not raw list-stream dumps.
- Native codec boundary: a clean-room component that reads and writes internal
  ordinary-form streams, container sections, and platform-shaped object records.
  It can later be exposed to Python through a process CLI, C ABI, or extension
  module.
- Platform oracle boundary: batch 1C runs that use `ПолучитьФорму`,
  `ЗначениеВСтрокуВнутр`, and `ЗначениеИзСтрокиВнутр` to produce evidence for
  the native codec. This remains an optional validation/runtime extraction tool,
  not a required parser dependency.

The native component must be developed as a sidecar first. Moving Python logic
into native code before oracle coverage exists would only move uncertainty into
a harder-to-debug language.

## Runtime Oracle Loop

For each form candidate:

1. Start from a clean ignored output directory such as
   `scan-output/runtime-oracle/<run-id>/`.
2. Export or load the configuration/processor into a temporary infobase.
3. In ordinary enterprise mode, materialize the target form:
   - external processor/report: create or execute the processor, then call its
     `ПолучитьФорму("<form-name>")` where supported by the object context;
   - configuration metadata object: create/get a runtime object or manager
     instance and call `ПолучитьФорму("<form-name>")` on the object/manager that
     owns the form.
4. Serialize the live form with `ЗначениеВСтрокуВнутр`.
5. If testing rebuild, deserialize a candidate stream with
   `ЗначениеИзСтрокиВнутр`, then serialize it back with
   `ЗначениеВСтрокуВнутр`.
6. Compare three internal artifacts:
   - source `Form.bin` form payload after repository unpack;
   - platform runtime string from the live form;
   - rebuilt/native candidate after platform deserialize/serialize.
7. Store only sanitized reports in OACS. Full streams, private configuration
   dumps, EPF/ERF files, and local paths stay under ignored directories.

## Native Component Phases

1. List-stream text codec: parser, compact writer, platform-style writer, and
   structural statistics. This is the current sidecar seed.
2. Form.bin section container: native split/join with byte-level tests against
   the Python implementation.
3. Ordinary object records: descriptors for `cf_form_controls8`,
   `cf_form_controls_position8`, and `cf_form_controls_info8` profiles, guided
   by runtime oracle evidence.
4. Public model bridge: native emits and accepts typed object-model DTOs. Raw
   streams remain internal diagnostics only.
5. Host integration: choose process CLI first for stability; later add a C ABI
   or Python extension if profiling proves the process boundary is too slow.

## Why Not Embed Platform Code

The native component should be open, auditable, and clean-room. It can use the
installed 1C platform as an oracle during tests, but it must not copy, vendor,
or link proprietary platform internals into the product. The reliable path is
to observe behavior through supported runtime serialization and strict Designer
validation, then implement compatible codecs with tests.
