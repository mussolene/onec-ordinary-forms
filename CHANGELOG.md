# Changelog

## 1.0.0 - Unreleased

- Establishes the v1 direction: native descriptor-backed ordinary form object
  graph as the canonical product architecture, with `Form.xml` and `Form.bin`
  as projections of that object graph.
- Breaking change: pre-v1 `Form.xml` and public raw/list-stream/profile/slot
  structures are not accepted. Re-run `dump-bin` from the original `Form.bin`
  instead of migrating old XML by hand.
- Release is not ready until the v1 release gate passes: all 417 known
  ordinary-form property rows classified and handled, corpus semantic diff
  gates clean, strict Designer validation clean, and Linux/macOS/Windows wheels
  plus sdist published to GitHub Release and PyPI.
