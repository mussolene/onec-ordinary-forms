# Changelog

## 1.0.0 - Unreleased

- Establishes the native descriptor-backed ordinary form object graph as the
  canonical product architecture, with `Form.xml` and `Form.bin` as
  projections of that object graph.
- Breaking change: legacy `Form.xml` with top-level `<Pages>`, deprecated
  `version="*"` attributes, and public raw/list-stream/profile/slot structures
  are not accepted. Re-run `dump-bin` from the original `Form.bin` instead of
  migrating old XML by hand.
- Public `Form.xml` now uses managed-form-style `ChildItems` nesting and
  `ordinaryFormVersion="2.0"` per `OrdinaryForm.xsd`.
- Release is not ready until the release gate passes: all 417 known
  ordinary-form property rows classified and handled, corpus semantic diff
  gates clean, strict Designer validation clean, and Linux/macOS/Windows wheels
  plus sdist published to GitHub Release and PyPI.
- Platform palette metadata lives in `OrdinaryFormPalette.xsd`; the public
  release gate rejects unmapped, no-public-XML, and XSD-only property gaps.
