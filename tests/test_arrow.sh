#!/bin/bash

set -o errexit

DIR=`dirname $0`
TEST_OUT=$DIR/test_arrow.out
IQ="$1 iquery --afl --query"
F=/tmp/1

PYTHON=python3
if [ -e "/usr/bin/python3.7" ]
then
    PYTHON=python3.7
fi

function iq() {
    $IQ "$1" >> $TEST_OUT
}

: > $TEST_OUT                   # Reset output file


echo -e "\nI. Basic"
echo "1. Single chunk (int64)"
iq "aio_save(apply(build(<x:int64>[i=-10:10:0:21], i), y, iif(i%2=0, i, int64(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas())" \
    >> $TEST_OUT

echo "2. Multiple chunks (int64)"
iq "aio_save(apply(build(<x:int64>[i=1:100:0:20], i), y, iif(i%2=0, i, int64(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "3. binary"
$PYTHON -c "import numpy, scidbpy; scidbpy.connect().input('<x:binary not null>[i=1:20:0:4]', upload_data=numpy.array([bytes(b'bin')] * 20, dtype='object')).apply('y', 'iif(i%2=0, x, binary(null))').store('bin')"
iq "aio_save(bin, '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "4. bool"
iq "aio_save(apply(build(<x:bool not null>[i=1:20:0:4], i), y, iif(i%2=0, bool(i), bool(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "5. char"
iq "aio_save(apply(build(<x:char not null>[i=1:20:0:4], 48 + i), y, iif(i%2=0, char(48 + i), char(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "6. datetime"
iq "aio_save(apply(build(<x:datetime not null>[i=1:20:0:4], i), y, iif(i%2=0, datetime(i), datetime(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "7. datetimetz - not supported"

echo "8. double"
iq "aio_save(apply(build(<x:double not null>[i=1:20:0:4], i), y, iif(i%2=0, double(i), double(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "9. float"
iq "aio_save(apply(build(<x:float not null>[i=1:20:0:4], i), y, iif(i%2=0, float(i), float(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "10. int8"
iq "aio_save(apply(build(<x:int8 not null>[i=1:20:0:4], i), y, iif(i%2=0, int8(i), int8(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "11. int16"
iq "aio_save(apply(build(<x:int16 not null>[i=1:20:0:4], i), y, iif(i%2=0, int16(i), int16(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "12. int32"
iq "aio_save(apply(build(<x:int32 not null>[i=1:20:0:4], i), y, iif(i%2=0, int32(i), int32(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "13. int64"
iq "aio_save(apply(build(<x:int64 not null>[i=1:20:0:4], i), y, iif(i%2=0, int64(i), int64(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "14. string"
iq "aio_save(apply(build(<x:string not null>[i=1:20:0:4], i), y, iif(i%2=0, string(i), string(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "15. uint8"
iq "aio_save(apply(build(<x:uint8 not null>[i=1:20:0:4], i), y, iif(i%2=0, uint8(i), uint8(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "16. uint16"
iq "aio_save(apply(build(<x:uint16 not null>[i=1:20:0:4], i), y, iif(i%2=0, uint16(i), uint16(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "17. uint32"
iq "aio_save(apply(build(<x:uint32 not null>[i=1:20:0:4], i), y, iif(i%2=0, uint32(i), uint32(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "18. uint64"
iq "aio_save(apply(build(<x:uint64 not null>[i=1:20:0:4], i), y, iif(i%2=0, uint64(i), uint64(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "19. int64, double, string"
iq "aio_save(apply(build(<x:int64>[i=1:20:0:4], i), y, iif(i%2=0, double(i), double(null)), z, iif(i%2=0, string(i), string(null))), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "20. Empty chunk"
iq "aio_save(filter(build(<x:int64>[i=1:20:0:4], i), x < 6 or x > 14), '$F', format:'arrow')"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "21. Different output sinks"
$IQ "aio_save(build(<x:int64>[i=0:0], i), 'console', format:'arrow')" \
    >> $TEST_OUT
$IQ "aio_save(build(<x:int64>[i=0:0], i), 'stdout', format:'arrow')" \
    >> $TEST_OUT
$IQ "aio_save(build(<x:int64>[i=0:0], i), 'stderr', format:'arrow')" \
    >> $TEST_OUT

iq "remove(bin)"


echo -e "\nII. Exceptions"
echo "1. datetimetz not supported"
$IQ "aio_save(build(<x:datetimetz>[i=0:0], apply_offset(datetime(i), 0)), '$F', format:'arrow')" 2>&1 \
    |  sed --expression='s/ line: [0-9]\+//g'                                                         \
    |  grep --invert-match "Failed query id:" >> $TEST_OUT                                            \
    || echo "expected exception"

echo "2. atts_only value"
$IQ "aio_save(build(<x:int64>[i=-10:10:0:21], i), '$F', format:'arrow', atts_only:'foo')" 2>&1  \
    |  sed --expression='s/ line: [0-9]\+//g'                                                   \
    |  grep --invert-match "Failed query id:" >> $TEST_OUT                                      \
    || echo "expected exception"

echo "3. atts_only repeat"
$IQ "aio_save(build(<x:int64>[i=-10:10:0:21], i), '$F', atts_only:true, format:'arrow', atts_only:false)" 2>&1     \
    |  sed --expression='s/ line: [0-9]\+//g'                                                                   \
    |  grep --invert-match "Failed query id:" >> $TEST_OUT                                                      \
    || echo "expected exception"


echo -e "\nIII. int64, int64(null)"
echo "1. store"
time $IQ "set no fetch; store(apply(build(<x:int64 not null>[i=1:3100000], i), y, iif(i%2=0, i * i, int64(null))), foo)" \
     >> $TEST_OUT
iq "summarize(foo)"

echo "2. save"
time $IQ "save(foo, '$F', 0, '(int64, int64 null)')" \
     >> $TEST_OUT
sz=`stat --printf="%s" $F`
echo "$((sz / 1024 / 1024)) MB ($sz B)" \
     >> $TEST_OUT

echo "3. aio_save(binary)"
time $IQ "aio_save(foo, '$F', format:'(int64, int64 null)')" \
     >> $TEST_OUT
sz=`stat --printf="%s" $F`
echo "$((sz / 1024 / 1024)) MB ($sz B)" \
     >> $TEST_OUT

echo "4. aio_save(arrow)"
time $IQ "aio_save(foo, '$F', format:'arrow')" \
     >> $TEST_OUT
sz=`stat --printf="%s" $F`
echo "$((sz / 1024 / 1024)) MB ($sz B)" \
     >> $TEST_OUT

# echo "5. Arrow read"
# time $PYTHON -c "import pyarrow; print(len(pyarrow.ipc.open_stream(open('$F', 'rb')).read_pandas()))" \
#      >> $TEST_OUT

echo "6. SciDB-Py fetch"
time $PYTHON -c "import scidbpy; print(len(scidbpy.connect().arrays.foo.fetch(atts_only=True)))" \
     >> $TEST_OUT

echo "7. SciDB-Py fetch w/ Arrow"
time $PYTHON -c "import scidbpy; print(len(scidbpy.connect().arrays.foo.fetch(atts_only=True, use_arrow=True)))" \
     >> $TEST_OUT

iq "remove(foo)"


echo -e "\nIV. int64, string(null)"
echo "1. store"
time $IQ "set no fetch; store(apply(build(<x:int64 not null>[i=1:2700000], i), y, iif(i%2=0, string(i * i), string(null))), foo)" \
     >> $TEST_OUT
iq "summarize(foo)"

echo "2. save"
time $IQ "save(foo, '$F', 0, '(int64, string null)')" \
     >> $TEST_OUT
sz=`stat --printf="%s" $F`
echo "$((sz / 1024 / 1024)) MB ($sz B)" \
     >> $TEST_OUT

echo "3. aio_save(binary)"
time $IQ "aio_save(foo, '$F', format:'(int64, string null)')" \
     >> $TEST_OUT
sz=`stat --printf="%s" $F`
echo "$((sz / 1024 / 1024)) MB ($sz B)" \
     >> $TEST_OUT

echo "4. aio_save(arrow)"
time $IQ "aio_save(foo, '$F', format:'arrow')" \
     >> $TEST_OUT
sz=`stat --printf="%s" $F`
echo "$((sz / 1024 / 1024)) MB ($sz B)" \
     >> $TEST_OUT

# echo "5. Arrow read"
# time $PYTHON -c "import pyarrow; print(len(pyarrow.ipc.open_stream(open('$F', 'rb')).read_pandas()))" \
#      >> $TEST_OUT

echo "6. SciDB-Py fetch"
time $PYTHON -c "import scidbpy; print(len(scidbpy.connect().arrays.foo.fetch(atts_only=True)))" \
     >> $TEST_OUT

echo "7. SciDB-Py fetch w/ Arrow"
time $PYTHON -c "import scidbpy; print(len(scidbpy.connect().arrays.foo.fetch(atts_only=True, use_arrow=True)))" \
     >> $TEST_OUT


iq "remove(foo)"


echo -e "\nIV. atts_only"
echo "1. Single chunk (atts_only=1)"
iq "aio_save(apply(build(<x:int64>[i=-10:10:0:21], i), y, iif(i%2=0, i, int64(null))), '$F', format:'arrow', atts_only:1)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas())" \
    >> $TEST_OUT

echo "2. Single chunk (atts_only=0)"
iq "aio_save(apply(build(<x:int64>[i=-10:10:0:21], i), y, iif(i%2=0, i, int64(null))), '$F', format:'arrow', atts_only:0)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas())" \
    >> $TEST_OUT

echo "3. Multiple chunks (atts_only=1)"
iq "aio_save(apply(build(<x:int64>[i=1:100:0:20], i), y, iif(i%2=0, i, int64(null))), '$F', format:'arrow', atts_only:1)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "4. Multiple chunks (atts_only=0)"
iq "aio_save(apply(build(<x:int64>[i=1:100:0:20], i), y, iif(i%2=0, i, int64(null))), '$F', format:'arrow', atts_only:0)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "5. datetime (atts_only=0)"
iq "aio_save(apply(build(<x:datetime not null>[i=1:20:0:4], i), y, iif(i%2=0, datetime(i), datetime(null))), '$F', format:'arrow', atts_only:0)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "6. double (atts_only=0)"
iq "aio_save(apply(build(<x:double not null>[i=1:20:0:4], i), y, iif(i%2=0, double(i), double(null))), '$F', format:'arrow', atts_only:0)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "7. float (atts_only=0)"
iq "aio_save(apply(build(<x:float not null>[i=1:20:0:4], i), y, iif(i%2=0, float(i), float(null))), '$F', format:'arrow', atts_only:0)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "8. int8 (atts_only=0)"
iq "aio_save(apply(build(<x:int8 not null>[i=1:20:0:4], i), y, iif(i%2=0, int8(i), int8(null))), '$F', format:'arrow', atts_only:0)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "9. string (atts_only=0)"
iq "aio_save(apply(build(<x:string not null>[i=1:20:0:4], i), y, iif(i%2=0, string(i), string(null))), '$F', format:'arrow', atts_only:0)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "10. uint32 (atts_only=0)"
iq "aio_save(apply(build(<x:uint32 not null>[i=1:20:0:4], i), y, iif(i%2=0, uint32(i), uint32(null))), '$F', format:'arrow', atts_only:0)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT

echo "11. Buffer size"
iq "aio_save(filter(build(<x:int64>[i=1:100:0:50], i), x % 2 = 0), '$F', format:'arrow', atts_only:0, buffer_size:256)"
$PYTHON -c "import pyarrow; print(pyarrow.ipc.open_stream(open('$F', 'rb')).read_all().to_pandas().sort_values('x').to_string(index=False))" \
    >> $TEST_OUT


sed --in-place 's/ instance: s0-.*//' $TEST_OUT

diff --ignore-all-space $TEST_OUT $DIR/test_arrow.expected
rm $TEST_OUT
