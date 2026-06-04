#!/usr/bin/env bash
set -euo pipefail

container_name=${OOF_PLATFORM_CONTAINER:-oof-1c85-licensed}
image=${OOF_PLATFORM_IMAGE:-ghcr.io/mussolene/1c-developer:8.5.1.1343}
platform_version=${OOF_PLATFORM_VERSION:-8.5.1.1343}
network_mode=${OOF_PLATFORM_NETWORK:-host}

if [[ -z "${OOF_NETHASP_INI:-}" || ! -r "$OOF_NETHASP_INI" ]]; then
  echo "OOF_NETHASP_INI must point to the ignored local nethasp.ini" >&2
  exit 2
fi

repo_root=$(pwd)
case "$OOF_NETHASP_INI" in
  ~/*) nethasp_input="${HOME}${OOF_NETHASP_INI#~}" ;;
  *) nethasp_input="$OOF_NETHASP_INI" ;;
esac
nethasp_path="$(cd "$(dirname "$nethasp_input")" && pwd -P)/$(basename "$nethasp_input")"

docker rm -f "$container_name" >/dev/null 2>&1 || true
docker run -d \
  --name "$container_name" \
  --platform linux/amd64 \
  --network "$network_mode" \
  --entrypoint sh \
  -v "$repo_root:/workspace" \
  -v "$nethasp_path:/opt/1cv8/conf/nethasp.ini:ro" \
  "$image" \
  -lc 'sleep infinity' >/dev/null

docker exec "$container_name" sh -lc "set -eu
  version_conf='/opt/1cv8/x86_64/$platform_version/conf'
  mkdir -p \"\$version_conf\"
  cp /opt/1cv8/conf/nethasp.ini \"\$version_conf/nethasp.ini\"
  printf 'DisableUnsafeActionProtection=.*\\n' >/opt/1cv8/conf/conf.cfg
  printf 'DisableUnsafeActionProtection=.*\\n' >\"\$version_conf/conf.cfg\"
"

echo "$container_name"
