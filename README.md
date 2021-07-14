accelerated_io_tools
==========

[![SciDB 19.11](https://img.shields.io/badge/SciDB-19.11-blue.svg)](https://forum.paradigm4.com/t/scidb-release-19-11/2411)
[![arrow 3.0.0](https://img.shields.io/badge/arrow-3.0.0-blue.svg)](https://arrow.apache.org/release/3.0.0.html)
[![Build Status](https://github.com/Paradigm4/SciDB-Py/actions/workflows/test.yml/badge.svg)](https://github.com/Paradigm4/accelerated_io_tools/actions/workflows/test.yml)

A prototype library for the accelerated import and export of data out of SciDB. The work started previously as the `prototype_load_tools` package and continued to get optimized and expanded. Currently contains two SciDB operators and several functions:
 * `aio_input`: an operator that reads token-separated text from 1 or more filesystem objects and returns the data as a SciDB array
 * a few scalar string functions - `dcast`, `trim`, `nth_tdv`, etc that can be useful in some data loading scenarios
 * `aio_save`: an operator that exports SciDB arrays into 1 or more filesystem objects, as token-separated text or binary files

The package extends regular SciDB IO and provides benefits in a few areas:

#### 1. Fully distributed parsing and packing
When loading token-delimited data, the instance(s) reading the data use a fixed-size `read` call - usually reading multiple megabytes at once. The read blocks are sent around the different SciDB instances as quickly as possible. Then, the "ragged line ending" of each block is separated and sent to the instance containing the next block. Finally, all the instances parse the data and populate the resulting array in parallel. Thus the expensive parsing step is almost fully parallelized; the ingest rate scales up with the number of instances.

When saving data, the reverse process is used: each instance packs its data into fixed-size blocks, then streams down to one or more saving instances. Save can also be done in binary form for faster speed.

#### 2. Loading from multiple files
When the parsing is so distributed, we find the read speed of the IO device is often the load bottleneck. To go around this, `aio_input` can be told to load data from 6 different files, for example. In such a case, 6 different SciDB instances, will open up 6 different files, on 6 different IO devices. The file pieces will then be quickly scattered across the whole SciDB cluster - up to perhaps 128 instances. Then the parallel parsing will begin. In reverse, saving to K different files is also possible.

The load from K files or save to K files happens transactionally as a single SciDB query. The user does not need to worry about firing up multiple "writer" processes and managing them. Simply provide a list of file paths and SciDB will handle the rest.

#### 3. Error tolerance
The `aio_input` operator ingests data in spite of extraneous characters, ragged rows that contain too few or too many columns, or columns that are mostly numeric but sometimes contain characters. Such datasets can be loaded easily into temporary arrays. SciDB can then be effectively used to find errors and fix them as needed.

The `accelerated_io_tools` and regular `prototype_load_tools` libraries cannot coexist on the same installation; the user must load one or the other. The accelerated .so is superior in every way.

# Trivial end-to-end example
Using a tiny file that is malformed on purpose. A toy example for those who may not be familiar with SciDB.
```
$ cat /tmp/foo.tsv
1   alex
2   bob
3   jack
4   dave    extra stuff here
5error_no_tab
```

Ingest the file into an array called temp:
```
$ iquery -anq "store(aio_input('/tmp/foo.tsv', num_attributes:2), temp)"
Query was executed successfully

$ iquery -aq "scan(temp)"
{tuple_no,dst_instance_id,src_instance_id} a0,a1,error
{0,0,0} '1','alex',null
{1,0,0} '2','bob',null
{2,0,0} '3','jack',null
{3,0,0} '4','dave','long    extra stuff here'
{4,0,0} '5error_no_tab',null,'short'
```

File errors can be identified quickly:
```
$ iquery -aq "filter(temp, error is not null)"
{tuple_no,dst_instance_id,src_instance_id} a0,a1,error
{3,0,0} '4','dave','long    extra stuff here'
{4,0,0} '5error_no_tab',null,'short'
```

Let's take the non-error rows, and redimension them to a new array, using the first column as a dimension:
```
$ iquery -anq "store(redimension(apply(filter(temp, error is null), dim, dcast(a0, int64(null)), val, a1), <val:string null>[dim=0:*,1000000,0]), foo)"
Query was executed successfully

$ iquery -aq "scan(foo)"
{dim} val
{1} 'alex'
{2} 'bob'
{3} 'jack'
```

Save the array foo to another file:
```
$ iquery -aq "aio_save(foo, '/tmp/bar.out')"
{chunk_no,dest_instance_id,src_instance_id} val

$ cat /tmp/bar.out
alex
bob
jack
```

Or if you'd like to output the dimensions:
```
$ iquery -aq "aio_save(project(apply(foo, d, dim), d, val),  '/tmp/bar2.out')"

$ cat /tmp/bar2.out
1   alex
2   bob
3   jack
```

The following sections describe the various options, and parameters in detail.

# Operator aio_input()
Going back to the above example:
```
$ cat /tmp/foo.tsv
1   alex
2   bob
3   jack
4   dave    extra stuff here
5error_no_tab

$ iquery -aq "aio_input('/tmp/foo.tsv', num_attributes:2)"
{tuple_no,dst_instance_id,src_instance_id} a0,a1,error
{0,0,0} '1','alex',null
{1,0,0} '2','bob',null
{2,0,0} '3','jack',null
{3,0,0} '4','dave','long    extra stuff here'
{4,0,0} '5error_no_tab',null,'short'
```
Note the extra `error` attribute is added and is not null whenever the input line of text does not match the specified number of attributes. The given filesystem object is opened and read once with the open/read/close call family; it can be a file, symlink, fifo or any other object that supports these calls.

`aio_input` can skip errors detected in the input with `skip:errors`:
```
$ iquery -anq "store(aio_input('/tmp/foo.tsv', num_attributes:2, skip:errors), temp2)"
Query was executed successfully

$ iquery -aq "scan(temp2)"
{tuple_no,dst_instance_id,src_instance_id} a0,a1,error
{0,0,0} '1','alex',null
{1,0,0} '2','bob',null
{2,0,0} '3','jack',null
```
Alternatively, `aio_input` can load only those lines which have errors with `skip:non-errors`.  By default, `aio_input` will not skip anything, and the keyword can be omitted altogether or present as `skip:nothing`.


Example CSV ingest and store from multiple files:
```
$ iquery -anq "store(
 aio_input(
  paths:('/tmp/foo.tsv','/tmp/foo2.tsv'),
  instances:(1,2),
  num_attributes:4,
  attribute_delimiter:',',
  split_on_dimension:true,
  header:1
 ),
 temp
)"
```

## Formally:
All parameters are supplied as key:value syntax. The `num_attributes` and `path` must always be specified:
```
aio_input(parameter:value, parameter2(value2,value3),...)
```

### Load from one or multiple files:
* `paths:'/path/to/file'`: the absolute path to load from. Assumed to be on the coordinator instance. If the operator encounters a string without a keyword specified, it uses that as path.
* `paths:('/path/to/file1','/path/to/file2')`: a comma separated list of paths for loading from multiple fs devices.

If more than one path is specified, then `instances` must be used to specify the loading instance identifiers:
* `instances:(0,1);...`: a comma separated list of instance ids, in the same order as `paths`. Must match the number of `paths` and contain unique ids.

### File format settings:
* `num_attributes:N`: number of columns in the file (at least on the majority of the lines). Required.
* `header:H`: an integer number of lines to skip from the file;  if "paths" is used, applies to all files. Default is 0.
* `line_delimiter:'L'`: a character that separates the lines (cells) of the file; values of `\t` `\r` `\n` and ` ` are also supported. Default is `\n`.
* `attribute_delimiter:A`: a character that separates the columns (attributes) of the file; values of `\t` `\r` `\n` and ` ` are also supported. Default is `\t`.
* `skip:S`: tells `aio_input` to skip `errors`, `non-errors`, or `nothing` (the default).  Use this keyword to skip errors, rather than `filter`, when reading from input.

### Splitting on dimension:
* `split_on_dimension:<true/false>`: a flag that determines whether the file columns are placed in SciDB attributes, or cells along an extra dimension. Default is `false` (create attributes).

### Tuning settings:
* `buffer_size:B`: the units into which the loaded file(s) are initially split when first redistributed across the cluster, specified in bytes; default is 8MB.
* `chunk_size:C`: the chunk size along the third dimension of the result array. Should not be required often as the `buffer_size` actually controls how much data goes in each chunk. Default is 10,000,000. If `buffer_size` is set and `chunk_size` is not set, the `chunk_size` is automatically set to equal `buffer_size` as an over-estimate.

### Returned array:
If `split_on_dimension:false` (default), the schema of the returned array is as follows:
```
 <a0:string null, a1:string null, ... aN-1: string null, error:string null>
 [tuple_no        = 0: *,                 CS,   0,
  dst_instance_id = 0: NUM_INSTANCES-1,   1,    0,
  src_instance_id = 0: NUM_INSTANCES-1,   1,    0]
```
Where `N` is the specified `num_attributes` value, `CS` is the chunk size (10M default; see above) and `NUM_INSTANCES` is the number of SciDB instances in the cluster. The error attribute is null, unless the particular line in the file had a number of tokens not equal to `N`, in which case the error attribute is set to either 'short' or 'long ' followed by the leftover line. In the case of a short line, the absent attributes are set to null.

If `split_on_dimension:true` the attributes are populated along a fourth dimension like so:
```
 <a:string null>
 [tuple_no        = 0: *,                 CS,   0,
  dst_instance_id = 0: NUM_INSTANCES-1,   1,    0,
  src_instance_id = 0: NUM_INSTANCES-1,   1,    0,
  attribute_no    = 0: N,                 N+1,  0]
```
The slice of the array at `attribute_no:N` shall contain the error attribute, populated as above.

Other than `attribute_no` (when `split_on_dimension:true`) the dimensions are not intended to be used in queries. The `src_instance_id` matches the instance(s) reading the data; the `dst_instance_id` is assigned in a round-robin fashion to successive blocks from the same source. The `tuple_no` starts at 0 for each `{dst_instance_id, src_instance_id}` pair and is populated densely within the block. However, each new block starts a new chunk.

# Scalar functions that may be useful in loading data

## dcast(): error-tolerant casting
SciDB supports regular type casting but the behavior is to fail the entire query if any cast is unsuccessful. This may not be desirable when the user expects a small percentage of error values and has a different strategy for handling them, converting to null for example. In those cases, `dcast` can be used to cast a string to a double, float, bool, int{64,32,16,8} or uint{64,32,16,8}, substituting in a special default value to return if the cast fails. The two arguments to the function are:
 * the string value to cast
 * the default value to use if the cast fails (often a null).
The SciDB typesystem and the type of the second value can be used to dispatch the right dcast return type.
For example, this cast fails on the fifth row:
```
$ iquery -aq "aio_input('/tmp/foo.tsv', num_attributes:2)"
{tuple_no,dst_instance_id,src_instance_id} a0,a1,error
{0,0,0} '1','alex',null
{1,0,0} '2','bob',null
{2,0,0} '3','jack',null
{3,0,0} '4','dave','long    extra stuff here'
{4,0,0} '5error_no_tab',null,'short'
$ iquery -aq "apply(aio_input('/tmp/foo.tsv', num_attributes:2), d0, double(a0))"
UserException in file: src/query/BuiltInFunctions.inc function: CONV_TID_DOUBLE_FROM_String line: 394
Error id: scidb::SCIDB_SE_TYPESYSTEM::SCIDB_LE_FAILED_PARSE_STRING
Error description: Typesystem error. Failed to parse string '5error_no_tab
```

And we can use dcast to return a null instead of failing the query:
```
$ iquery -aq "apply(aio_input('/tmp/foo.tsv', num_attributes:2), d0, dcast(a0, double(null)))"
{tuple_no,dst_instance_id,src_instance_id} a0,a1,error,d0
{0,0,0} '1','alex',null,1
{1,0,0} '2','bob',null,2
{2,0,0} '3','jack',null,3
{3,0,0} '4','dave','long    extra stuff here',4
{4,0,0} '5error_no_tab',null,'short',null
```

We can also use a missing code, or a special non-null value like `-1`:
```
$ iquery -aq "apply(aio_input('/tmp/foo.tsv', num_attributes:2), d0, dcast(a0, double(missing(1))))"
{tuple_no,dst_instance_id,src_instance_id} a0,a1,error,d0
{0,0,0} '1','alex',null,1
{1,0,0} '2','bob',null,2
{2,0,0} '3','jack',null,3
{3,0,0} '4','dave','long    extra stuff here',4
{4,0,0} '5error_no_tab',null,'short',?1
$ iquery -aq "apply(aio_input('/tmp/foo.tsv', num_attributes:2), d0, dcast(a0, double(-1)))"
{tuple_no,dst_instance_id,src_instance_id} a0,a1,error,d0
{0,0,0} '1','alex',null,1
{1,0,0} '2','bob',null,2
{2,0,0} '3','jack',null,3
{3,0,0} '4','dave','long    extra stuff here',4
{4,0,0} '5error_no_tab',null,'short',-1
```

dcast ignores any whitespace preceding or following the numeric value. When converting to an unsigned type, a negative input is considered non-convertible and the supplied default is returned. An input out of range is also considered non-convertible for integers (overflow will not happen). When converting to float or double, all ranges of values are supported but inf or -inf may be returned if the input exceeds machine limits. When converting to bool, values of 0,N,NO,F,FALSE or 1,Y,YES,T,TRUE (ignore case) are supported. Internally strtod, strtoll, strtoull are used - see those routines for details on locale sensitivity and what "whitespace" means.

## trim() removes specific characters from the beginning and end of a string:
```
$ iquery -aq "
 project(
  apply(
   filter(
    tmp,
    not (line_no=0 and chunk_no=0)
   ),
  ta0, trim(a0, '\"')),
 a0, ta0
 )"
{src_instance_id,chunk_no,line_no} a0,ta0
{0,0,1} '"alex"','alex'
{0,1,0} '"b"ob"','b"ob'
{0,1,1} 'jake','jake'
{0,2,0} 'random','random'
{0,2,1} 'bill','bill'
{0,3,0} 'alice','alice'

$ iquery -aq "
 project(
  apply(
   filter(
    tmp,
    not (line_no=0 and chunk_no=0)
   ),
   ta0, trim(a0, '\"b')
  ),
  a0, ta0
 )"
{src_instance_id,chunk_no,line_no} a0,ta0
{0,0,1} '"alex"','alex'
{0,1,0} '"b"ob"','o'
{0,1,1} 'jake','jake'
{0,2,0} 'random','random'
{0,2,1} 'bill','ill'
{0,3,0} 'alice','alice'
```

## char_count() counts the occurrences of a particular character or list of characters:
```
$ iquery -aq "apply(build(<val:string>[i=0:0,1,0], 'abc, def, xyz'), cc, char_count(val, 'x'))"
{i} val,cc
{0} 'abc, def, xyz',1

$ iquery -aq "apply(build(<val:string>[i=0:0,1,0], 'abc, def, xyz'), cc, char_count(val, ','))"
{i} val,cc
{0} 'abc, def, xyz',2
```

## nth_tdv() and nth_csv() extract a substring from a field that contains delimiters.
Adjacent delimiters return the empty string ('').
Indices greater than than the number of delimiters return null
```
$ iquery -aq "apply(build(<val:string>[i=0:0,1,0], 'abc,,xyz'), n, nth_csv(val, 0))"
{i} val,n
{0} 'abc,,xyz','abc'

$ iquery -aq "apply(build(<val:string>[i=0:0,1,0], 'abc,,xyz'), n, nth_csv(val, 1))"
{i} val,n
{0} 'abc,,xyz',''

$ iquery -aq "apply(build(<val:string>[i=0:0,1,0], 'abc,,xyz'), n, nth_csv(val, 3))"
{i} val,n
{0} 'abc,def,xyz',null
```
nth_tdv is more general: it lets you specify one or more delimiting characters:
```
$ iquery -aq "apply(build(<val:string>[i=0:0,1,0], 'abc def/xyz'), n, nth_tdv(val, 1, ' /'))"
{0} 'abc def/xyz','def'
```
Note the benefit of using small cross_joins to decompose compound fields.
The iif is present to overcome a limitation in SciDB's logic that determines when an attribute can be nullable,
the iif can be skipped if you know exactly how many fields there are:
```
$ iquery -aq "
 apply(
  cross_join(
   build(
    <val:string>[i=0:0,1,0],
    'abc, def, xyz'),
   build(
    <x:int64>[j=0:3,4,0], j)
  ),
  n, iif(nth_csv(val, j) is null, null, nth_csv(val,j))
 )"
{i,j} val,x,n
{0,0} 'abc, def, xyz',0,'abc'
{0,1} 'abc, def, xyz',1,' def'
{0,2} 'abc, def, xyz',2,' xyz'
{0,3} 'abc, def, xyz',3,null
```
Similarly, maxlen_csv() and maxlen_tdv() first split a string along a delimiter but then return the length of the longest field as an integer.

## keyed_value() pulls values out of key-value lists
It expects an input in the form of "KEY1=VALUE1;KEY2=VALUE2;.." and returns a value for a given key name. The third argument is a default to return when the key is not found:
```
$ iquery -aq "
 apply(
  build(
   <val:string>[i=0:0,1,0], 'LEN=43;WID=35.3'),
  l, double(keyed_value(val, 'LEN', null)),
  w, double(keyed_value(val, 'WID', null))
 )"
{i} val,l,w
{0} 'LEN=43;WID=35.3',43,35.3
```

## throw() terminates a query with an error:
```
$ iquery -aq "apply(apply(build(<val:string>[i=0:0,1,0], 'LEN=43;WID=35.3'), l, double(keyed_value(val, 'LEN', null)), w, double(keyed_value(val, 'WID', null))), input_check, iif(w < 50, true, throw('Invalid Width')))"
{i} val,l,w,input_check
{0} 'LEN=43;WID=35.3',43,35.3,true

$ iquery -aq "
 apply(
  apply(
   build(
    <val:string>[i=0:0,1,0], 'LEN=43;WID=35.3'
   ),
   l, double(keyed_value(val, 'LEN', null)),
   w, double(keyed_value(val, 'WID', null))
  ),
  input_check, iif(w < 30, true, throw('Invalid Width'))
 )"
SystemException in file: Functions.cpp function: toss line: 392
Error id: scidb::SCIDB_SE_INTERNAL::SCIDB_LE_ILLEGAL_OPERATION
Error description: Internal SciDB error. Illegal operation: Invalid Width
```

## codify() converts a given string to its ascii representation:
```
$ iquery -aq "project(apply(tmp, ca0, codify(a0)), a0, ca0)"
{src_instance_id,chunk_no,line_no} a0,ca0
{0,0,0} 'col1','99|111|108|49|0|'
{0,0,1} '"alex"','34|97|108|101|120|34|0|'
{0,1,0} '"b"ob"','34|98|34|111|98|34|0|'
{0,1,1} 'jake','106|97|107|101|0|'
{0,2,0} 'random','114|97|110|100|111|109|0|'
{0,2,1} 'bill','98|105|108|108|0|'
{0,3,0} 'alice','97|108|105|99|101|0|'
```

For an example of using regular expressions, consult the regular expression substitution routine provided in https://github.com/paradigm4/superfunpack

# Operator aio_save()
This operator replaces the existing save functionality, for binary and tab-delimited formats.
Example save to a binary file:
```
iquery -anq "aio_save( bar, '/tmp/bar.out', format:'(int64, double null, string null)')"
```

Example save to a binary file using Apache Arrow format:
```
iquery -anq "aio_save( bar, '/tmp/bar.out', format:'arrow')"
```

Example save to two TSV files:
```
iquery -anq "aio_save(
 filter(bar, a>3),
 paths:('/tmp/bar1.tsv','/tmp/bar2.tsv'),
 instances:0;16,
 format:'tdv'
)
```

## Formally:
```
aio_save(array, parameter1:value1, parameter2:value2,...)
```
The `paths` must always be specified.

Arrow support is built by default.  However, if you'd like to build without arrow because it's not available for your system, then remove the `-DUSE_ARROW` flags from `src/Makefile` to remove arrow support at compile time.

## Save to one or more files:
By default, the file is saved to a path on the query coordinator instance. You can distribute the IO and network load by simultaneously writing data to multiple FS devices from several different instances (one instance per path).
* `paths:'/path/to/file'`: the location to save the file; required. If the operator enocunters a string parameter without a keyword specifier, it assumes that to be the path.
* `paths:('/path1','/path2'..`: multiple file paths for saving from different instances, separated by semicolon. Must be specified along with `instances` and have an equal number of terms.
* `instances:I`: the instance to save the file on. Default is the instance the query is submitted to.
* `instances:(0,1);..`: multiple instance ID's for saving from different instances, separated by semicolon. Must be specified along with `paths` and have an equal number of unique terms. Either `instance` or `instances` must be specified, but not both.

## Other settings:
* `format:'F'`: the format string, may be either `tdv` (token-delimited values), a scidb-style binary format spec like `(int64, double null,...)`, or `arrow` for the Apache Arrow format. Default is `tdv`.
* `attributes_delimiter:'A'`: the character to write between array attributes. Default is a tab. Applies when format is set to `tdv`.
* `line_delimiter"'L'`: the character to write between array cells. Default is a newline. Applies when format is set to `tdv`.
* `cells_per_chunk:'C'`: the maximum number of array cells to place in each chunk before saving to disk. By default, binary accounting is used but this can be enabled to force an exact number of cells. See notes on saving data in order below.
* `buffer_size:'B'`: the amount of data to pack into a single buffer before transferring and saving to disk. Default is 8 MB. This setting is not honored if `cells_per_chunk` is specified.
* `precision:'P'`: the maximum number of significant figures to use when writing float or double values as text. Defaults to the SciDB 'precision' config. Applies when format is set to `tdv`.
* `atts_only:true`: specify whether the output should only include attribute values or include attribute as well as dimension values. Possible values are `false` and `true` (default). If `atts_only:false` is specified the dimension values are appended for each cell after the attribute values. The type used for the dimension values is `int64`. This setting is only applicable when the binary or the `arrow` formats are used. For the binary format, the `format:'(...)'` specification has to include an `int64` type specifications (appended at the end) for each of the input array dimensions.
* `result_size_limit:M`: absolute limit of the output file in Megabytes. By default it is set to 2^64-1.

## Returned array:
The schema is always `<val:string null> [chunk_no=0:*,1,0, src_instance_id=0:*,1,0]`. The returned array is always empty as the operator's objective is to export the data.

## Saving data in order:
Note that the order of the returned data is arbitrary. If the client requires data in specific order, they must:
 0. save from a single instance
 1. add a sort operator
 2. enable the `cells_per_chunk` setting and set it to match the sort chunk size (1M default)
 3. add an explicit `_sg` operator between the sort and the save - with round-robin distribution

For example:
```
aio_save(
 _sg(
  sort(bar, attribute),
  1
 ),
 '/path/to/file',
 format:'tdv',
 cells_per_chunk:'100000'
)
```

# Installation

There are multiple ways to install the `accelerated_io_tools` package:

* [Install `extra-scidb-libs`](#install-extra-scidb-libs)
* [Install using `dev_tools`](#install-using-dev_tools)
* [Install from source](#install-from-source)

Once installed, the plugin can be loaded with:

```bash
> iquery -aq "load_library('accelerated_io_tools')"
```

If you are using Shim together with SciDB, note that you can configure
shim to use `aio_save` after it is installed to speed up data exports.
See Shim [Help](http://paradigm4.github.io/shim/help.html#aio-plugin)
page for instructions.

## Install extra-scidb-libs

The easiest way to install `accelerated_io_tools` is to install
[`extra-scidb-libs`](https://paradigm4.github.io/extra-scidb-libs/)

## Install using dev_tools

### Install prerequisites

The following libraries are required to build the plugin:

* SciDB development libraries
* PostgreSQL development library
* Apache Arrow development library version `3.0.0`

#### RHEL/CentOS 7

1. Install the Extra Packages for Enterprise Linux (EPEL) repository
   (see [instructions](https://fedoraproject.org/wiki/EPEL)), if not
   already installed.
1. Add the SciDB Extra Libs repository:
   ```bash
   > cat <<EOF | sudo tee /etc/yum.repos.d/scidb-extra.repo
   [scidb-extra]
   name=SciDB extra libs repository
   baseurl=https://downloads.paradigm4.com/extra/$SCIDB_VER/centos7
   gpgcheck=0
   enabled=1
   EOF
   ```

##### Apache Arrow

The Apache Arrow library and SciDB are compiled using different
tool-chains. As a consequence, the Apache Arrow library needs to be
compiled from source.

```
curl --location \
    "https://www.apache.org/dyn/closer.lua?action=download&filename=arrow/arrow-3.0.0/apache-arrow-3.0.0.tar.gz" \
    | tar --extract --gzip
cd apache-arrow-3.0.0/cpp
mkdir build
cd build
scl enable devtoolset-3                                             \
    "cmake3 ..                                                      \
         -DARROW_WITH_LZ4=ON                                        \
         -DARROW_WITH_ZLIB=ON                                       \
         -DCMAKE_CXX_COMPILER=/opt/rh/devtoolset-3/root/usr/bin/g++ \
         -DCMAKE_C_COMPILER=/opt/rh/devtoolset-3/root/usr/bin/gcc   \
         -DCMAKE_INSTALL_PREFIX=/opt/apache-arrow"
make
make install
```

#### Ubuntu Xenial

1. Install the `apt-transport-https` package (if not already installed):
   ```bash
   > sudo apt-get install apt-transport-https
   ```
1. Add the SciDB Extra Libs repository:
   ```bash
   > cat <<APT_LINE | sudo tee /etc/apt/sources.list.d/scidb-extra.list
   deb https://downloads.paradigm4.com/ extra/$SCIDB_VER/ubuntu14.04/
   APT_LINE
   > sudo apt-get update
   ```
1. Use Apache Arrow [instructions](https://arrow.apache.org/install/)
   for setting up the Apache Arrow repository
1. Install the dependencies:
   ```bash
   > sudo apt-get install scidb-$SCIDB_VER-dev libboost-system1.58-dev liblog4cxx10-dev \
       libprotobuf-dev libpqxx-dev libarrow-dev=3.0.0-1
   ```

### Install the plugin

After that, follow
[`dev-tools` installation](https://github.com/paradigm4/dev_tools#installation)
instructions to get `dev_tools` first. Then:
```bash
> iquery -aq "install_github('paradigm4/accelerated_io_tools')"
```

## Install from source

Follow the [Install Prerequisites](#install-prerequisites)
above. Download the sourcecode from GitHub and run `make` in the top
directory. Copy the resulting `.so` file to your SciDB plugins
directory as instructed by the `make` output.

## Note: unload prototype_load_tools if loaded

Warning: if you were previously using `prototype_load_tools` you will
need to unload that library and restart the cluster:

* `iquery -aq "unload_library('prototype_load_tools')"`
* restart the cluster
* `iquery -aq "load_library('accelerated_io_tools')"`

## Note: use the right branch for your version

The git branches of accelerated_io_tools follow different versions of
SciDB - with the master branch used for the most recent version. Note
also changes in behavior are possible between versions.

## Note: Old split() and parse() operators

Are included in this .so as well and work the same way as before. They
are, however, deprecated. Operator `aio_input` should be used instead.
