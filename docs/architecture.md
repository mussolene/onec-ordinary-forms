# Architecture Notes

The repository is split by format boundary, not by CLI command.

## Layers

- `formbin.py` owns the ordinary `Form.bin` section container. It parses,
  unpacks, and packs sections byte-for-byte, including the service sections
  that are not decoded yet.
- `bracket.py` owns the ordinary form bracket stream. It converts that stream
  into the legacy `elem-json` shape used by the current XML writer.
- `pipeline.py` owns orchestration between formats. For example, `dump-bin`
  means `Form.bin -> section files -> elem-json -> object-model XML`, but this
  module does not know the XML schema details.
- `cli.py` owns command-line argument parsing and the current object-model XML
  writer/rebuild code.
- `corpus.py` owns portable corpus and exported-form scanning.

## Current Direction

The next cleanup target is to move object-model XML writing/rebuild helpers out
of `cli.py` into a dedicated model module. After that, `cli.py` should contain
only thin command wrappers.

Behavioral changes should stay separate from these moves. A pure architecture
cleanup must keep CLI arguments stable and pass byte-for-byte no-op rebuild
checks.
