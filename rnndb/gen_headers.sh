#!/bin/bash
# Generate headers using `headergen`
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export RNN_PATH=${DIR}
HEADERGEN=${DIR}/../envytools/build/rnn/headergen
${HEADERGEN} isa.xml
${HEADERGEN} cmdstream.xml
${HEADERGEN} state.xml
mv ${DIR}/*.xml.h ${DIR}/../native/include/etna

