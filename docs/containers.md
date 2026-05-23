# Container Validation

Container runs are optional local validation tools. They are not part of the
published Python package.

Do not commit platform archives, mounted private volumes, license files,
generated exports, EPF/ERF processors, or command logs that contain local
machine details.

## Expected Inputs

- A 1C platform image that contains `ibcmd` and Designer batch mode.
- A readable local license configuration referenced by
  `NETHASP_INI_PATH`.
- Private EPF/ERF fixtures stored outside git or under ignored directories.

The repository helper:

```bash
export NETHASP_INI_PATH="<local-nethasp.ini>"
tools/platform_validate_epf.sh /path/to/processor.epf
```

The helper mounts the license configuration read-only, runs the platform in an
isolated container, and writes generated output under ignored `scan-output/`.

## Validation Rule

For ordinary form writer work, prefer platform Designer export/import checks.
Metadata-only checks can miss malformed ordinary `Form.bin` streams that later
fail with "Ошибка формата потока".

Record only sanitized command outcomes in OACS evidence: command class, pass or
fail, relevant exit code, and a short error summary. Keep private file paths,
license data, and full platform dumps out of memory and git.
