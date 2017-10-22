#!/bin/bash
# Usage: build_json.sh <executable> <gcabi>
#
# build structure definition for GCS hal interface from dwarf debug information in native executable.
# Needs dwarf_to_c (https://www.github.com/laanwj/dwarf_to_c)
extract_structures_json.py $1 _gcsHAL_INTERFACE _gcoCMDBUF _gcsQUEUE > data/gcs_hal_interface_$2.json
