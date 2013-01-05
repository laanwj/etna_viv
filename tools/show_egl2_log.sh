#!/bin/bash
# Usage: show_egl2_log.sh ../native/egl2/cube.fdr <flags>
#
#  -l, --hide-load-state
#                        Hide "LOAD_STATE" entries, this can make command
#                        stream a bit easier to read
#  --show-state-map      Expand state map from context (verbose!)
#  --show-context-commands
#                        Expand context command buffer (verbose!)
#  --show-context-buffer
#                        Expand context CPU buffer (verbose!)
#  --list-address-states
#                       When dumping command buffer, provide list of states
#                        that contain GPU addresses
#  --dump-shaders       Dump binary shaders to disk
#
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
python dump_cmdstream.py $* ${DIR}/data/gcs_hal_interface_v2.json ${DIR}/../rnndb/state.xml ${DIR}/../rnndb/isa.xml
