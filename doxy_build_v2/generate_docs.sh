#!/usr/bin/env bash
set -euo pipefail

VERSION=""
SAVE_ARTIFACTS=false
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

version_suffix() {
    if [[ -n "$VERSION" ]]; then
        echo "_${VERSION}"
    fi
}

build_base_components() {
    echo "=== Base Components PDF ==="
    doxygen Doxyfile
    make -C latex
    cp latex/refman.pdf "base_components_umd$(version_suffix).pdf"
}

build_ttdevice_reference() {
    echo "=== TTDevice Reference PDF ==="
    doxygen Doxyfile_ttdevice
    make -C latex_ttdevice
    cp latex_ttdevice/refman.pdf "base_tt_device_reference$(version_suffix).pdf"
}

build_mapping_table() {
    local src="$1"
    local out="$2"
    echo "=== ${out} ==="
    pandoc "$src" \
        -f markdown+raw_html -t html5 --standalone --self-contained \
        --shift-heading-level-by=-1 \
        --css pandoc_table.css \
        -o "$out"
}

build_chip_mapping() {
    build_mapping_table base_api_mapping_table.md "chip_tt_device_mapping$(version_suffix).html"
}

build_cluster_mapping() {
    build_mapping_table workload_api_mapping_table.md "cluster_tt_device_mapping$(version_suffix).html"
}

build_exalens_mapping() {
    build_mapping_table exalens_umd.md "tt_exalens_tt_umd_mapping$(version_suffix).html"
}

build_all() {
    build_base_components
    build_ttdevice_reference
    build_chip_mapping
    build_cluster_mapping
    build_exalens_mapping
}

cleanup_artifacts() {
    if [[ "$SAVE_ARTIFACTS" == false ]]; then
        rm -rf latex/ latex_ttdevice/
    fi
}

clean() {
    echo "=== Cleaning ==="
    rm -rf latex/ latex_ttdevice/
    rm -f base_components_umd*.pdf base_tt_device_reference*.pdf
    rm -f chip_tt_device_mapping*.html cluster_tt_device_mapping*.html tt_exalens_tt_umd_mapping*.html
}

usage() {
    cat <<'EOF'
Usage: generate_docs.sh [OPTIONS]

Options:
  --version=X.Y.Z     Append version to output filenames (omit for no version suffix)
  --all               Generate all documents (default when no target specified)
  --base-components   Base components PDF only
  --ttdevice-ref      TTDevice reference PDF only
  --chip-mapping      Chip mapping HTML only
  --cluster-mapping   Cluster mapping HTML only
  --exalens-mapping   Exalens mapping HTML only
  --save-artifacts    Keep intermediate latex/ and latex_ttdevice/ directories
  --clean             Remove all generated files
  --help, -h          Show this help

Examples:
  ./generate_docs.sh                                    # Generate all docs, no version suffix
  ./generate_docs.sh --version=1.0                      # Generate all docs with _1.0 suffix
  ./generate_docs.sh --version=2.0 --chip-mapping       # Single doc with version
  ./generate_docs.sh --base-components --ttdevice-ref    # Two specific docs
  ./generate_docs.sh --version=1.0 --save-artifacts     # Keep latex build artifacts
  ./generate_docs.sh --clean                             # Remove all generated files
EOF
}

TARGETS=()

for arg in "$@"; do
    case "$arg" in
        --version=*)        VERSION="${arg#--version=}" ;;
        --save-artifacts)   SAVE_ARTIFACTS=true ;;
        --all)              TARGETS+=(all) ;;
        --base-components)  TARGETS+=(base_components) ;;
        --ttdevice-ref)     TARGETS+=(ttdevice_ref) ;;
        --chip-mapping)     TARGETS+=(chip_mapping) ;;
        --cluster-mapping)  TARGETS+=(cluster_mapping) ;;
        --exalens-mapping)  TARGETS+=(exalens_mapping) ;;
        --clean)            clean; exit 0 ;;
        --help|-h)          usage; exit 0 ;;
        *)                  echo "Unknown option: $arg"; usage; exit 1 ;;
    esac
done

if [[ ${#TARGETS[@]} -eq 0 ]]; then
    TARGETS=(all)
fi

for target in "${TARGETS[@]}"; do
    case "$target" in
        all)              build_all ;;
        base_components)  build_base_components ;;
        ttdevice_ref)     build_ttdevice_reference ;;
        chip_mapping)     build_chip_mapping ;;
        cluster_mapping)  build_cluster_mapping ;;
        exalens_mapping)  build_exalens_mapping ;;
    esac
done

cleanup_artifacts
echo "=== Done ==="
