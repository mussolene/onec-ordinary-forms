#!/usr/bin/env bash
# Run a complete diagnostic BSL module in a synthetic ordinary-form source.
set -euo pipefail

usage() {
  cat <<'TXT'
Usage: tools/platform_oracle_execute.sh <synthetic-source-root.xml> <script.bsl> <out-dir>

Requires OOF_PLATFORM_CONTAINER to name an already-running licensed 1C
container. Source must be under work/oracle-runtime/ or scan-output/.
The caller supplies the complete Module.bsl and receives OutputDir through
ПараметрЗапуска. Results are saved in a new directory under scan-output/.
TXT
}
if [[ $# -eq 1 && $1 == --help ]]; then usage; exit 0; fi
if [[ $# -ne 3 ]]; then usage >&2; exit 2; fi
if [[ -z ${OOF_PLATFORM_CONTAINER:-} ]]; then
  echo "Set OOF_PLATFORM_CONTAINER to an already-running licensed 1C container" >&2
  exit 2
fi

repo_root=$(cd "$(dirname "$0")/.." && pwd -P)
native_bin=${OOF_NATIVE_BIN:-$repo_root/build/sidecars/onec-form-native/oof}
source_root=$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")
script_path=$(cd "$(dirname "$2")" && pwd -P)/$(basename "$2")
case "$source_root" in
  "$repo_root"/work/oracle-runtime/*/root.xml|"$repo_root"/scan-output/*/root.xml) ;;
  *) echo "source-root.xml must be a synthetic source under work/oracle-runtime/ or scan-output/" >&2; exit 2 ;;
esac
if [[ ! -f "$source_root" || ! -f "$script_path" || ! -x "$native_bin" ]]; then
  echo "source root, BSL script, or built oof CLI is missing" >&2
  exit 2
fi
if [[ -L "$source_root" || -L "$script_path" ]]; then
  echo "source root and BSL script must not be symbolic links" >&2
  exit 2
fi

if ! out_dir=$(python3 - "$repo_root" "$3" <<'PY'
import os
import pathlib
import sys

repo = pathlib.Path(sys.argv[1]).resolve()
raw = sys.argv[2]
if os.path.basename(raw.rstrip("/")) in ("", ".", ".."):
    raise SystemExit("out-dir must have a normal final path component")
candidate = pathlib.Path(raw)
if not candidate.is_absolute():
    candidate = repo / candidate
if candidate.is_symlink():
    raise SystemExit("out-dir must not be a symbolic link")
resolved = candidate.resolve(strict=False)
scan_root = (repo / "scan-output").resolve()
try:
    relative = resolved.relative_to(scan_root)
except ValueError:
    raise SystemExit("out-dir must resolve under repository scan-output/")
if not relative.parts:
    raise SystemExit("out-dir must be a new directory below scan-output/")
print(resolved)
PY
); then
  echo "invalid out-dir; it must resolve below repository scan-output/" >&2
  exit 2
fi
if [[ -e "$out_dir" || -L "$out_dir" ]]; then
  echo "out-dir must not already exist" >&2
  exit 2
fi
mkdir -p "$out_dir/source" "$out_dir/logs" "$out_dir/output"
cp -R "$(dirname "$source_root")/." "$out_dir/source/"
if find "$out_dir/source" -type l -print -quit | rg -q .; then
  echo "synthetic source tree must not contain symbolic links" >&2
  exit 2
fi
if find "$out_dir/source" -type f \( -iname '*.epf' -o -iname '*.erf' \) -print -quit | rg -q .; then
  echo "synthetic source tree must not contain compiled EPF/ERF files" >&2
  exit 2
fi
metadata_root="$out_dir/source/root.xml"
if [[ ! -f "$metadata_root" ]]; then
  echo "source tree must contain root.xml beside the supplied source root" >&2
  exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 is required to set the named ExternalDataProcessor.DefaultForm" >&2
  exit 2
fi

if ! python3 - "$metadata_root" "$out_dir/source/root/Forms" >"$out_dir/logs/metadata-edit.log" 2>&1 <<'PY'
import pathlib
import sys
import xml.etree.ElementTree as ET

metadata_path = pathlib.Path(sys.argv[1])
forms_dir = pathlib.Path(sys.argv[2])
for _, (prefix, uri) in ET.iterparse(metadata_path, events=("start-ns",)):
    ET.register_namespace(prefix, uri)
tree = ET.parse(metadata_path)
root = tree.getroot()
local = lambda element: element.tag.rsplit("}", 1)[-1]
processors = [element for element in root if local(element) == "ExternalDataProcessor"]
if len(processors) != 1:
    raise SystemExit("root.xml must declare exactly one ExternalDataProcessor")
processor = processors[0]
properties = next((node for node in processor if local(node) == "Properties"), None)
children = next((node for node in processor if local(node) == "ChildObjects"), None)
if properties is None or children is None:
    raise SystemExit("ExternalDataProcessor must have Properties and ChildObjects")
name_node = next((node for node in properties if local(node) == "Name"), None)
form_nodes = [node for node in children if local(node) == "Form"]
if name_node is None or not (name_node.text or "").strip() or len(form_nodes) != 1:
    raise SystemExit("ExternalDataProcessor needs a name and exactly one Form")
form_name = (form_nodes[0].text or "").strip()
if not form_name or not (forms_dir / (form_name + ".xml")).is_file():
    raise SystemExit("the unique ChildObjects Form metadata file is missing")
form_files = list(forms_dir.glob("*.xml"))
if len(form_files) != 1 or form_files[0].name != form_name + ".xml":
    raise SystemExit("source metadata directory must contain exactly the declared Form")
expected_bin = forms_dir / form_name / "Ext" / "Form.bin"
actual_bins = list(metadata_path.parent.glob("**/Forms/*/Ext/Form.bin"))
if len(actual_bins) != 1 or actual_bins[0].resolve() != expected_bin.resolve():
    raise SystemExit("the unique Form.bin must belong to the declared default Form")
default_form = next((node for node in properties if local(node) == "DefaultForm"), None)
if default_form is None:
    namespace = properties.tag[1:].split("}", 1)[0] if properties.tag.startswith("{") else ""
    qualified_name = "{" + namespace + "}DefaultForm" if namespace else "DefaultForm"
    default_form = ET.SubElement(properties, qualified_name)
default_form.text = "ExternalDataProcessor." + name_node.text.strip() + ".Form." + form_name
tree.write(metadata_path, encoding="utf-8", xml_declaration=True)
print(default_form.text)
PY
then
  echo "cannot assign unambiguous named DefaultForm; see logs/metadata-edit.log" >&2
  exit 2
fi

form_bins=()
while IFS= read -r -d '' path; do form_bins+=("$path"); done < <(
  find "$out_dir/source" -path '*/Forms/*/Ext/Form.bin' -type f -print0
)
if [[ ${#form_bins[@]} -ne 1 ]]; then
  echo "synthetic source must contain exactly one Forms/*/Ext/Form.bin" >&2
  exit 2
fi
form_bin=${form_bins[0]}
form_xml="${form_bin%/Form.bin}/Form.xml"
module_file="${form_bin%/Form.bin}/Form/Module.bsl"
mkdir -p "$(dirname "$form_xml")" "$(dirname "$module_file")"
if ! "$native_bin" dump "$form_bin" "$form_xml" >"$out_dir/logs/oof-dump.json" 2>"$out_dir/logs/oof-dump.stderr"; then
  echo "current oof dump rejected the synthetic Form.bin; see logs/oof-dump.*" >&2
  exit 1
fi
cp "$script_path" "$module_file"
if ! "$native_bin" build "$form_xml" "$form_bin" >"$out_dir/logs/oof-build.json" 2>"$out_dir/logs/oof-build.stderr"; then
  echo "current oof build rejected the named model; see logs/oof-build.*" >&2
  exit 1
fi

if [[ "$(docker inspect -f '{{.State.Running}}' "$OOF_PLATFORM_CONTAINER" 2>/dev/null || true)" != true ]]; then
  echo "OOF_PLATFORM_CONTAINER is not an already-running container" >&2
  exit 2
fi
if ! container_base=$(docker exec "$OOF_PLATFORM_CONTAINER" mktemp -d /tmp/oof-button-oracle.XXXXXX); then
  echo "cannot allocate an isolated temporary directory in the platform container" >&2
  exit 1
fi
cleanup() {
  docker exec "$OOF_PLATFORM_CONTAINER" rm -rf -- "$container_base" >/dev/null 2>&1 || true
}
trap cleanup EXIT
docker exec "$OOF_PLATFORM_CONTAINER" mkdir -p "$container_base/source" "$container_base/logs" "$container_base/dbroot" "$container_base/output"
docker cp "$out_dir/source/." "$OOF_PLATFORM_CONTAINER:$container_base/source/"

set +e
docker exec -i "$OOF_PLATFORM_CONTAINER" sh -s -- "$container_base" <<'SH'
container_base=$1
base="$container_base/dbroot"
db=db

set +e
/opt/1cv8/x86_64/8.5.1.1343/ibcmd infobase \
  --data="$base" --database-path="$db" create --locale=ru_RU \
  >"$container_base/logs/create.log" 2>&1
code=$?
set -e
if [ "$code" -ne 0 ]; then
  printf '%s\n' "$code" >"$container_base/logs/platform-code.txt"
  exit "$code"
fi

set +e
xvfb-run -a timeout 120 /opt/1cv8/x86_64/8.5.1.1343/1cv8 DESIGNER \
  /F "$base/$db" \
  /LoadExternalDataProcessorOrReportFromFiles \
  "$container_base/source/root.xml" "$container_base/oracle.epf" \
  /Out "$container_base/logs/load.log" -NoTruncate /DisableStartupDialogs \
  >"$container_base/logs/load-stdout.log" 2>"$container_base/logs/load-stderr.log"
code=$?
set -e
if [ "$code" -ne 0 ]; then
  printf '%s\n' "$code" >"$container_base/logs/platform-code.txt"
  exit "$code"
fi

set +e
xvfb-run -a timeout 300 /opt/1cv8/x86_64/8.5.1.1343/1cv8 ENTERPRISE \
  /F "$base/$db" /RunModeOrdinaryApplication \
  /Execute "$container_base/oracle.epf" \
  /C "OutputDir=$container_base/output" \
  /Out "$container_base/logs/enterprise.log" -NoTruncate /DisableStartupDialogs \
  >"$container_base/logs/enterprise-stdout.log" 2>"$container_base/logs/enterprise-stderr.log"
code=$?
set -e
printf '%s\n' "$code" >"$container_base/logs/platform-code.txt"
exit "$code"
SH
platform_code=$?
set -e
if docker exec "$OOF_PLATFORM_CONTAINER" test -d "$container_base/logs"; then
  docker cp "$OOF_PLATFORM_CONTAINER:$container_base/logs/." "$out_dir/logs/"
fi
if docker exec "$OOF_PLATFORM_CONTAINER" test -d "$container_base/output"; then
  docker cp "$OOF_PLATFORM_CONTAINER:$container_base/output/." "$out_dir/output/"
fi
printf '%s\n' "$platform_code" >"$out_dir/platform-code.txt"
if [[ $platform_code -ne 0 ]]; then
  echo "platform execution failed with code $platform_code; logs and output are preserved in $out_dir" >&2
  exit "$platform_code"
fi
echo "runtime output and logs: $out_dir"
