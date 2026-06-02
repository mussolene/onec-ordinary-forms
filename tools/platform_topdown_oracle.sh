#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'USAGE'
Usage: tools/platform_topdown_oracle.sh [options]

Top-down ordinary form evidence runner. It starts wide from platform resources
and XSD files, then narrows to GUID/control evidence actually present in real
Form.bin streams and, when possible, platform Designer dump output.

Options:
  --form-bin PATH       Analyze one Form.bin.
  --form-root DIR       Analyze every Forms/*/Ext/Form.bin below DIR.
  --source-root PATH    root.xml for platform oracle execute checks.
  --epf PATH            Also run strict platform dump validation for EPF/ERF.
  --out-dir DIR         Output directory inside repo (default:
                        scan-output/platform-topdown-oracle).
  --skip-internal-oracle
                        Do not run platform ValueFromStringInternal oracle.
  --skip-product-loop   Do not run current dump-bin/build-bin smoke loop.
  -h, --help            Show this help.

If neither --form-bin nor --form-root is provided, the script uses
work/oracle-runtime/runtime-source/root when present, otherwise the blank
oracle Form.bin sample.
USAGE
}

repo_root=$(pwd)
native_bin="sidecars/onec-form-native/build/oof-native"
out_dir="scan-output/platform-topdown-oracle"
form_bin=""
form_root=""
epf_path=""
source_root=""
run_product_loop=1
run_internal_oracle=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --form-bin)
      form_bin=${2:?}
      shift 2
      ;;
    --form-root)
      form_root=${2:?}
      shift 2
      ;;
    --epf)
      epf_path=${2:?}
      shift 2
      ;;
    --source-root)
      source_root=${2:?}
      shift 2
      ;;
    --out-dir)
      out_dir=${2:?}
      shift 2
      ;;
    --skip-product-loop)
      run_product_loop=0
      shift
      ;;
    --skip-internal-oracle)
      run_internal_oracle=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

case "$out_dir" in
  /*)
    out_abs=$out_dir
    ;;
  *)
    out_abs="$repo_root/$out_dir"
    ;;
esac

case "$out_abs" in
  "$repo_root"/scan-output/*) ;;
  *)
    echo "Output directory must be under scan-output/: $out_abs" >&2
    exit 2
    ;;
esac

if [[ -z "$form_bin" && -z "$form_root" ]]; then
  if [[ -d work/oracle-runtime/runtime-source/root ]]; then
    form_root="work/oracle-runtime/runtime-source/root"
  elif [[ -f work/oracle-runtime/blank-source/root/Forms/Форма/Ext/Form.bin ]]; then
    form_bin="work/oracle-runtime/blank-source/root/Forms/Форма/Ext/Form.bin"
  else
    echo "No default Form.bin corpus found; pass --form-bin or --form-root" >&2
    exit 2
  fi
fi

rm -rf "$out_abs"
mkdir -p "$out_abs"/{logs,resources,xsd,forms,product-loop,platform}

summary="$out_abs/summary.txt"
: > "$summary"

log_summary() {
  printf '%s\n' "$*" | tee -a "$summary"
}

log_summary "topdown-oracle-start"
log_summary "repo=$repo_root"
log_summary "out=$out_abs"
if [[ -n "$form_bin" ]]; then log_summary "form-bin=$form_bin"; fi
if [[ -n "$form_root" ]]; then log_summary "form-root=$form_root"; fi
if [[ -n "$epf_path" ]]; then log_summary "epf=$epf_path"; fi
if [[ -n "$source_root" ]]; then log_summary "source-root=$source_root"; fi

make -C sidecars/onec-form-native >"$out_abs/logs/native-build.log" 2>&1

if [[ ! -x "$native_bin" ]]; then
  echo "Native sidecar was not built: $native_bin" >&2
  exit 1
fi

find work/platform82-xml-resources -type f -name '*.xsd' -print 2>/dev/null \
  | sort > "$out_abs/xsd/files.txt" || true
xsd_count=$(wc -l < "$out_abs/xsd/files.txt" | tr -d ' ')
log_summary "xsd-files=$xsd_count"
if [[ "$xsd_count" -gt 0 ]]; then
  xargs "$native_bin" platform-xsd-inventory < "$out_abs/xsd/files.txt" \
    > "$out_abs/xsd/inventory.json"
fi

{
  find work/platform82-resource-files -type f -name '*.res' -print 2>/dev/null || true
  find work/platform82-container-libs -type f -name '*.res' -print 2>/dev/null || true
} | sort -u > "$out_abs/resources/files.txt"
resource_count=$(wc -l < "$out_abs/resources/files.txt" | tr -d ' ')
log_summary "resource-files=$resource_count"
if [[ "$resource_count" -gt 0 ]]; then
  xargs "$native_bin" platform-resource-descriptor-scan < "$out_abs/resources/files.txt" \
    > "$out_abs/resources/descriptor-scan.json"
fi

if [[ -n "$form_bin" ]]; then
  if [[ ! -f "$form_bin" ]]; then
    echo "Form.bin does not exist: $form_bin" >&2
    exit 2
  fi
  printf '%s\n' "$form_bin" > "$out_abs/forms/files.txt"
else
  if [[ ! -d "$form_root" ]]; then
    echo "Form root does not exist: $form_root" >&2
    exit 2
  fi
  find "$form_root" -path '*/Forms/*/Ext/Form.bin' -type f -print \
    | sort > "$out_abs/forms/files.txt"
fi

form_count=$(wc -l < "$out_abs/forms/files.txt" | tr -d ' ')
log_summary "form-bins=$form_count"
if [[ "$form_count" -eq 0 ]]; then
  echo "No Form.bin files selected" >&2
  exit 2
fi

idx=0
while IFS= read -r selected_form; do
  idx=$((idx + 1))
  form_out="$out_abs/forms/$idx"
  mkdir -p "$form_out"
  printf '%s\n' "$selected_form" > "$form_out/source-path.txt"
  "$native_bin" form-payload-structure "$selected_form" \
    > "$form_out/form-payload-structure.json"

  if [[ "$run_product_loop" -eq 1 ]]; then
    loop_out="$out_abs/product-loop/$idx"
    mkdir -p "$loop_out"
    if PYTHONPATH=src python3 -m onec_ordinary_forms.cli dump-bin \
      --bin "$selected_form" \
      --out "$loop_out/Form.xml" \
      > "$loop_out/dump.log" 2>&1; then
      if PYTHONPATH=src python3 -m onec_ordinary_forms.cli build-bin \
        --xml "$loop_out/Form.xml" \
        --asset-root "$loop_out" \
        --out-bin "$loop_out/Form.rebuilt.bin" \
        > "$loop_out/build.log" 2>&1; then
        if cmp -s "$selected_form" "$loop_out/Form.rebuilt.bin"; then
          printf 'byte-identical\n' > "$loop_out/result.txt"
        else
          printf 'rebuilt-differs\n' > "$loop_out/result.txt"
        fi
        "$native_bin" form-payload-structure "$loop_out/Form.rebuilt.bin" \
          > "$loop_out/rebuilt-form-payload-structure.json"
      else
        printf 'build-failed\n' > "$loop_out/result.txt"
      fi
    else
      printf 'dump-failed\n' > "$loop_out/result.txt"
    fi
  fi
done < "$out_abs/forms/files.txt"

if [[ "$run_internal_oracle" -eq 1 ]]; then
  oracle_dir="$out_abs/platform/internal-form"
  mkdir -p "$oracle_dir"
  first_form=$(sed -n '1p' "$out_abs/forms/files.txt")
  if [[ -z "$source_root" ]]; then
    case "$first_form" in
      */root/Forms/*/Ext/Form.bin)
        source_root="${first_form%%/root/Forms/*}/root.xml"
        ;;
    esac
  fi
  if [[ -z "$source_root" || ! -f "$source_root" ]]; then
    printf 'skipped: source root.xml not found; pass --source-root\n' \
      > "$oracle_dir/skipped.txt"
    log_summary "internal-form-oracle=skipped-missing-source-root"
  elif [[ -z "${OOF_PLATFORM_CONTAINER:-}" && ( -z "${NETHASP_INI_PATH:-}" || ! -r "${NETHASP_INI_PATH:-}" ) ]]; then
    printf 'skipped: OOF_PLATFORM_CONTAINER or readable NETHASP_INI_PATH is required\n' \
      > "$oracle_dir/skipped.txt"
    log_summary "internal-form-oracle=skipped-missing-platform-env"
  else
    mkdir -p "$oracle_dir/unpacked"
    PYTHONPATH=src python3 -m onec_ordinary_forms.cli unpack-bin \
      --bin "$first_form" \
      --out-dir "$oracle_dir/unpacked" \
      > "$oracle_dir/unpack.log" 2>&1
    set +e
    tools/platform_oracle_execute.sh \
      "$source_root" \
      "$oracle_dir/unpacked/Form.xml" \
      tools/platform_oracle_internal_form.bsl \
      "$oracle_dir/output.txt" \
      "$oracle_dir/execute" \
      > "$oracle_dir/wrapper.log" 2>&1
    oracle_code=$?
    set -e
    printf '%s\n' "$oracle_code" > "$oracle_dir/code.txt"
    log_summary "internal-form-oracle-code=$oracle_code"
  fi
fi

if [[ -n "$epf_path" ]]; then
  if [[ ! -f "$epf_path" ]]; then
    echo "EPF/ERF does not exist: $epf_path" >&2
    exit 2
  fi
  epf_native_dir="$out_abs/platform/epf-native"
  mkdir -p "$epf_native_dir/inflated" "$epf_native_dir/inner"
  if "$native_bin" container-extract-inflate "$epf_path" "$epf_native_dir/inflated" \
    > "$epf_native_dir/outer-container.json" 2>"$epf_native_dir/outer-container.err"; then
    native_inner_forms=0
    for candidate in "$epf_native_dir"/inflated/*; do
      [[ -f "$candidate" ]] || continue
      candidate_name=$(basename "$candidate")
      if "$native_bin" formbin-info "$candidate" > "$epf_native_dir/inner/$candidate_name.info.json" 2>/dev/null; then
        if grep -F -q '"name":"form"' "$epf_native_dir/inner/$candidate_name.info.json"; then
          inner_out="$epf_native_dir/inner/$candidate_name.files"
          mkdir -p "$inner_out"
          "$native_bin" container-extract "$candidate" "$inner_out" \
            > "$epf_native_dir/inner/$candidate_name.extract.json"
          "$native_bin" form-payload-structure "$candidate" \
            > "$epf_native_dir/inner/$candidate_name.form-payload-structure.json"
          native_inner_forms=$((native_inner_forms + 1))
        fi
      fi
    done
    log_summary "native-epf-inner-forms=$native_inner_forms"
  else
    log_summary "native-epf-extract=failed"
  fi
  if [[ -n "${OOF_PLATFORM_CONTAINER:-}" || ( -n "${NETHASP_INI_PATH:-}" && -r "${NETHASP_INI_PATH:-}" ) ]]; then
    set +e
    tools/platform_validate_epf.sh "$epf_path" "$out_abs/platform/validate" \
      > "$out_abs/platform/validate-wrapper.log" 2>&1
    platform_code=$?
    set -e
    printf '%s\n' "$platform_code" > "$out_abs/platform/validate-code.txt"
    log_summary "platform-validate-code=$platform_code"
  else
    printf 'skipped: OOF_PLATFORM_CONTAINER or readable NETHASP_INI_PATH is required\n' \
      > "$out_abs/platform/validate-skipped.txt"
    log_summary "platform-validate=skipped-missing-platform-env"
  fi
fi

if [[ "$run_product_loop" -eq 1 ]]; then
  find "$out_abs/product-loop" -name result.txt -type f -print0 \
    | xargs -0 cat 2>/dev/null \
    | sort \
    | uniq -c > "$out_abs/product-loop/result-frequency.txt" || true
  log_summary "product-loop-results:"
  sed 's/^/  /' "$out_abs/product-loop/result-frequency.txt" | tee -a "$summary"
fi

log_summary "topdown-oracle-done"
