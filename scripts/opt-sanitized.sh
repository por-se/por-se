#!/bin/bash

# this file provides a wrapper of 'opt' that LD_PRELOADs libasan and libubsan using the same shared objects that are used by klee

set -e
set -o pipefail
set -u

env LD_PRELOAD=$(ldd $(which klee) | grep "lib\(a\|ub\)san" | cut -d" " -f3 | sed "N;s/\n/:/g") opt "$@"
