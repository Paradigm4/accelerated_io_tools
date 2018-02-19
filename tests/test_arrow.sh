#!/bin/bash

set -o errexit

DIR=`dirname $0`
TEST_OUT=$DIR/test_arrow.out
IQ="iquery --afl --query"


$IQ "aio_save(apply(build(<x:int64>[i=-9:9], i), j, iif(i%2=0, i, int64(null))), '/tmp/1', 'format=arrow')" > $TEST_OUT
python -c "import pyarrow; print(pyarrow.open_stream('/tmp/1').read_all().to_pandas())" >> $TEST_OUT

$IQ "aio_save(build(<x:uint64>[i=0:0], i), '/tmp/1', 'format=arrow')" 2>&1 \
    |  sed --expression='s/ line: [0-9]\+//g' >> $TEST_OUT                 \
    || echo "expected exception"


diff $TEST_OUT $DIR/test_arrow.expected