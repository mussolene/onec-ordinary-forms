# 1C Platform Containers

This repository uses platform containers only as execution and inspection
environments. Do not commit platform archives, private volumes, `nethasp.ini`,
license data, EPF/ERF files, or exported customer metadata.

## Available Images

- `ghcr.io/mussolene/1c-developer:8.5.1.1343`
  - Linux amd64 1C 8.5 image.
  - Has `ibcmd` at `/opt/1cv8/x86_64/8.5.1.1343/ibcmd`.
  - Preferred for `ibcmd config export --file` experiments and round-trip
    checks.
- `ghcr.io/mussolene/1c82-platform:8.2.19.130`
  - Wine-based amd64 1C 8.2.19.130 image.
  - Contains Windows 8.2 binaries and DLLs under the Wine prefix, including
    `1cv8.exe`, `1cv8c.exe`, ordinary-form related DLLs, and HASP helper DLLs.
  - Useful for inspecting 8.2 libraries and validating behavior that depends on
    the old ordinary-form runtime.

Always run these images as `linux/amd64`. ARM containers are not a valid
execution target for these 1C platform checks.

## nethasp.ini

`nethasp.ini` is local license-server configuration. Get it from the
organization's 1C license administrator or copy the approved file from a
working developer workstation. Do not generate it from this repository, do not
commit it, and do not store its contents in OACS evidence or memory.

Use an environment variable pointing to the local file:

```bash
export NETHASP_INI_PATH="<local-nethasp.ini>"
test -r "$NETHASP_INI_PATH"
```

For this workstation, the local pointer is stored in the ignored file
`.agent/local/nethasp.env`:

```bash
. .agent/local/nethasp.env
test -r "$NETHASP_INI_PATH"
```

The ignored file may contain a host-specific absolute path. Keep it out of git
and out of OACS.

The exact contents depend on the organization's license server. If a platform
command reaches a license check and fails, first verify that the file is
mounted read-only into the platform's `conf` directory and that the container
has network access to the license server.

## 8.5 ibcmd Export

For external processors/reports, create a temporary file infobase first. Without
that step `ibcmd config export --file` can fail with a missing `1Cv8.1CD`
message even though the EPF/ERF file itself is readable.

```bash
docker run --rm --platform linux/amd64 \
  --entrypoint sh \
  -v "$PWD:/workspace" \
  -v "$NETHASP_INI_PATH:/opt/1cv8/conf/nethasp.ini:ro" \
  ghcr.io/mussolene/1c-developer:8.5.1.1343 \
  -lc 'set -eu
    ib=/tmp/ib
    mkdir -p "$ib"
    /opt/1cv8/x86_64/8.5.1.1343/ibcmd \
      infobase --data="$ib" --database-path=db create --locale=ru
    /opt/1cv8/x86_64/8.5.1.1343/ibcmd \
      config --data="$ib" --database-path=db export \
      --file /workspace/path/to/input.epf \
      --force /workspace/scan-output/exported'
```

Use private mounted source folders for real EPF/ERF corpus work, and keep
exports under ignored `scan-output/`, `work/`, or `/tmp`.

## 8.2 Wine Runtime

The 8.2 image is primarily for old-platform compatibility inspection and
runtime experiments:

```bash
docker run --rm --platform linux/amd64 \
  --entrypoint sh \
  -v "$NETHASP_INI_PATH:/opt/wineprefix/drive_c/Program Files/1cv82/conf/nethasp.ini:ro" \
  ghcr.io/mussolene/1c82-platform:8.2.19.130 \
  -lc 'wine "C:\\Program Files\\1cv82\\8.2.19.130\\bin\\1cv8c.exe" /?'
```

If license discovery is still inconsistent, also test mounting the same file to
the version-specific `conf` directory inside the Wine prefix. Keep all command
logs sanitized before recording them as OACS evidence.
