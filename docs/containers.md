# Container Validation

Container runs are optional local validation tools. They are not part of the
native release artifact.

Do not commit platform archives, mounted private volumes, license files,
generated exports, EPF/ERF processors, or command logs that contain local
machine details.

## Expected Inputs

- A 1C platform image that contains `ibcmd` and Designer batch mode.
- Preferred: an already configured licensed container, usually
  `oof-1c85-licensed` for 8.5 checks or `oof-1c82-licensed` for 8.2
  inspection.
- Fallback only: a readable local license configuration referenced by
  `NETHASP_INI_PATH` when creating a new throwaway platform container.
- Private EPF/ERF fixtures stored outside git or under ignored directories.

The repository helper:

```bash
export OOF_PLATFORM_CONTAINER=oof-1c85-licensed
tools/platform_validate_epf.sh /path/to/processor.epf
```

The helper copies only the current input and generated output through
`docker cp` and runs the platform inside the already licensed container. It
does not need or record the private `nethasp.ini` path.

If no licensed container is available, use the fallback:

```bash
export NETHASP_INI_PATH="<local-nethasp.ini>"
tools/platform_validate_epf.sh /path/to/processor.epf
```

That fallback mounts the license configuration read-only, runs the platform in
an isolated container, and writes generated output under ignored
`scan-output/`.

For local development, the preferred way to create the reusable licensed
container is:

```bash
export OOF_NETHASP_INI="<ignored-local-nethasp.ini>"
tools/platform_start_licensed_container.sh
export OOF_PLATFORM_CONTAINER=oof-1c85-licensed
```

The start helper uses host networking by default, mounts the ignored
`nethasp.ini` read-only, copies it into the version-specific platform
configuration directory inside the container, and writes
`DisableUnsafeActionProtection=.*` into both platform config locations. Do not
commit the local `nethasp.ini` or record its contents.

## Validation Rule

For ordinary form writer work, prefer platform Designer export/import checks.
Metadata-only checks can miss malformed ordinary `Form.bin` streams that later
fail with "Ошибка формата потока".

Record only sanitized command outcomes in OACS evidence: command class, pass or
fail, relevant exit code, and a short error summary. Keep private file paths,
license data, and full platform dumps out of memory and git.
