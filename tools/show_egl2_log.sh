#!/bin/bash
# Usage: show_egl2_log.sh ../native/egl2/cube.fdr <flags>
#   See ./dump_cmdstream.py --help for flags
#
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
if [ -z "$1" ]; then
    echo "Must provide at least the name of a .fdr file."
    echo "Usage: show_egl2_log.sh ../native/egl2/cube.fdr <flags>"
    echo
fi
python ${DIR}/dump_cmdstream.py $* ${DIR}/data/gcs_hal_interface_${GCABI}.json

