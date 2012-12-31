#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
./fdr_dump_mem.py ${DIR}/../native/egl/egl2.fdr 106 0x476f0700 0x5dc00 0x0
#./fdr_dump_mem.py ${DIR}/../native-tests/egl/egl2.fdr 85 0x44605e80 0x60000 0x7c24de80
#./fdr_dump_mem.py ${DIR}/../native-tests/egl/egl2.fdr 106 0x423bb200 0x700 0x0

