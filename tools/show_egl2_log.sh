#!/bin/bash
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
#  --disassembly         Disassemble shader instructions
#
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
python dump_cmdstream.py $* ${DIR}/../native/egl/egl2.fdr ${DIR}/data/gcs_hal_interface_v2.json ${DIR}/../rnndb/state.xml ${DIR}/../rnndb/isa.xml
