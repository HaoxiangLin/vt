#!/bin/bash

# get path to this file
DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
# include helper functions
. ${DIR}/../common/_helpers.sh
# put path to vt binary into handy variable
VT=${DIR}/../../vt
# immediately quit on errors
set -e

# create temporary directory and ensure cleanup on termination
export TMPDIR=$(mktemp -d)
trap "set -x; rm -rf ${TMPDIR}" EXIT KILL TERM INT HUP

echo "Tests for vt decompose_blocksub" >&2
echo "-------------------------------" >&2


# ---------------------------------------------------------------------------
# Test 01: block decomposition of even-length blocks
# ---------------------------------------------------------------------------

set -x
#mkdir -p ${TMPDIR}/01

# call program
${VT} \
    decompose_blocksub \
    ${DIR}/01_IN_even_length.vcf \
    >${TMPDIR}/01_OUT_even_length.vcf \
    2> >(strip_stderr > ${TMPDIR}/01_OUT_even_length.stderr)

# compare results
diff ${DIR}/01_OUT_even_length.vcf ${TMPDIR}/01_OUT_even_length.vcf
diff ${DIR}/01_OUT_even_length.stderr ${TMPDIR}/01_OUT_even_length.stderr

set +x

# ---------------------------------------------------------------------------
# Test 02: block decomposition with alignment
# ---------------------------------------------------------------------------

set -x
#mkdir -p ${TMPDIR}/02

# call program
${VT} \
    decompose_blocksub -a \
    ${DIR}/02_IN_uneven_length.vcf \
    >${TMPDIR}/02_OUT_uneven_length.vcf \
    2> >(strip_stderr > ${TMPDIR}/02_OUT_uneven_length.stderr) \

# compare results
diff ${DIR}/02_OUT_uneven_length.vcf ${TMPDIR}/02_OUT_uneven_length.vcf
diff ${DIR}/02_OUT_uneven_length.stderr ${TMPDIR}/02_OUT_uneven_length.stderr

set +x
