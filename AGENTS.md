## OACS Repo Workflow

For any non-trivial work in this repository, including form parser changes,
corpus experiments, platform export/rebuild checks, documentation changes, and
release work, use OACS as the durable project memory, context, and evidence
surface.

Required sequence:

1. State the task scope and explicit acceptance criteria (`AC1`, `AC2`, ...)
   before implementation.
2. Export repo-local ACS state before using ACS:
   `export OACS_DB="$PWD/.agent/oacs/oacs.db"`.
   Local development stores may use OACS `local_unlocked` key material.
3. Check the OACS consumer pack before substantial OACS-dependent work:
   `acs --version`.
4. Ask the reference context gate before building context:
   `acs context gate --intent repo_development --scope project --task "<task>" --json`.
   Treat `decision=build` as the signal to run `acs context build`. Treat
   `decision=skip` as valid only for tiny visible-file edits.
5. Query durable memory first, then build or inspect fresh context when the gate
   says `build`, when prior project memory/evidence may matter, or when in
   doubt:
   `acs memory query --query "<task intent>" --scope project --json` and
   `acs context build --intent "<task intent>" --scope project --json`.
6. Treat command outputs, Docker checks, OACS/MCP results, and runtime checks as
   evidence with `acs tool ingest-result ...`.
7. Promote verified reusable conclusions with `acs memory propose`,
   `acs memory commit`, and `acs memory sharpen`.
8. Record a checkpoint for each completed iteration with outcome, evidence refs,
   and next step: `acs checkpoint add ... --evidence <ev_...> --json`.
9. Before every commit, check staged changes for non-project information and
   sensitive data: no private EPF/ERF files, customer dumps, local host paths,
   `.env`, OACS DB files, `nethasp.ini` contents, credentials, tokens, license
   data, platform archives, local volumes, or unrelated artifacts.
10. Close each completed work iteration with a focused commit after checks pass.

Hard rules:

- Do not claim completion unless every acceptance criterion is `PASS`.
- Do not claim completion unless current verification, OACS evidence, and an
  OACS checkpoint exist for the iteration.
- Current code and current command results are the source of truth, not prior
  chat claims.
- Keep secrets out of OACS: no customer file paths, EPF payloads, ITS
  credentials, license data, `nethasp.ini` contents, platform archives, full
  help dumps, or local host paths.
- Do not read, print, or commit `.agent/oacs/key.json`,
  `.agent/oacs/unlocked.key`, `.agent/oacs`, `.oacs`, local databases,
  passphrases, or private agent state.
- Keep private processors and generated exports out of git. Use ignored
  `scan-output/`, `work/`, or `/tmp` for corpus experiments.
- Prefer platform `ibcmd` for export/import/rebuild validation. Do not route
  this repository's parser work through `vrunner` unless a task explicitly
  requires that separate tool.
