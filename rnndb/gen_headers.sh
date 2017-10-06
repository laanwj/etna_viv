#!/bin/bash
# Generate headers using `headergen`
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export RNN_PATH=${DIR}
HEADERGEN=${DIR}/../envytools/build/rnn/headergen
${HEADERGEN} isa.xml
${HEADERGEN} cmdstream.xml
${HEADERGEN} state.xml
${HEADERGEN} texdesc_3d.xml
rm -f ${DIR}/copyright.xml.h
mv ${DIR}/*.xml.h ${DIR}/../src/etnaviv

