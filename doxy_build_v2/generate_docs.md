# Generating UMD Base Layer Documents

All commands assume the working directory is the folder containing the
Doxyfile and the `*_doxy.hpp` source files.

The examples below use version `1.0` in output filenames. Replace with
the target version as needed (e.g. `base_components_umd_2.0.pdf`).

## Prerequisites

- `doxygen` (1.9.6+)
- `pdflatex` (TeX Live)
- `make`
- `pandoc` (2.9+)

## 1. Base Components (`base_components_umd_1.0.pdf`)

```bash
doxygen Doxyfile
cd latex && make && cp refman.pdf ../base_components_umd_1.0.pdf && cd ..
```

## 2. TTDevice Reference (`base_tt_device_reference_1.0.pdf`)

Uses `Doxyfile_ttdevice` which inherits from `Doxyfile` via `@INCLUDE`.
Includes the source listing from `examples/tt_device.hpp` and `examples/tt_device.cpp`.

```bash
doxygen Doxyfile_ttdevice
cd latex_ttdevice && make && cp refman.pdf ../base_tt_device_reference_1.0.pdf && cd ..
```

## 3. Chip Mapping (`chip_tt_device_mapping_1.0.html`)

```bash
pandoc base_api_mapping_table.md \
  -f markdown+raw_html -t html5 --standalone --self-contained \
  --shift-heading-level-by=-1 \
  --css pandoc_table.css \
  -o chip_tt_device_mapping_1.0.html
```

## 4. Cluster Mapping (`cluster_tt_device_mapping_1.0.html`)

```bash
pandoc workload_api_mapping_table.md \
  -f markdown+raw_html -t html5 --standalone --self-contained \
  --shift-heading-level-by=-1 \
  --css pandoc_table.css \
  -o cluster_tt_device_mapping_1.0.html
```

## 5. Exalens Mapping (`tt_exalens_tt_umd_mapping_1.0.html`)

```bash
pandoc exalens_umd.md \
  -f markdown+raw_html -t html5 --standalone --self-contained \
  --shift-heading-level-by=-1 \
  --css pandoc_table.css \
  -o tt_exalens_tt_umd_mapping_1.0.html
```
