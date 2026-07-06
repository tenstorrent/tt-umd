#!/usr/bin/env bash
set -euo pipefail

VERSION=""
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

clean() {
    echo "=== Cleaning ==="
    rm -rf latex/ latex_ttdevice/
    rm -f base_components_umd*.pdf base_tt_device_reference*.pdf
    rm -f chip_tt_device_mapping*.html cluster_tt_device_mapping*.html tt_exalens_tt_umd_mapping*.html
}

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --version=X.Y.Z     Append version to output filenames (omit for no version suffix)"
    echo "  --all               Generate all documents (default when no target specified)"
    echo "  --base-components   Base components PDF only"
    echo "  --ttdevice-ref      TTDevice reference PDF only"
    echo "  --chip-mapping      Chip mapping HTML only"
    echo "  --cluster-mapping   Cluster mapping HTML only"
    echo "  --exalens-mapping   Exalens mapping HTML only"
    echo "  --clean             Remove all generated files"
    echo "  --help, -h          Show this help"
}

TARGETS=()

for arg in "$@"; do
    case "$arg" in
        --version=*)        VERSION="${arg#--version=}" ;;
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

echo "=== Done ==="
