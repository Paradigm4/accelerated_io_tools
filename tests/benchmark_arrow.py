import os
import scidbpy
import sys
import timeit


ar_names = ('bm_fix', 'bm_var')
ar_schemas = [None, None]


def setup(mb):
    cnt = mb * 1024 * 1024 / 8 / 4

    db = scidbpy.connect()

    ar_name = ar_names[0]
    db.build('<z:int64 not null>[i=1:{}]'.format(cnt),
             'random()'
             ).apply('y', 'int64(random())',
                     'x', 'int64(random())',
                     'w', 'int64(random())'
                     ).store(ar_name)
    ar_schemas[0] = db.arrays[ar_name].schema()
    scidb_bytes_fix = db.summarize(ar_name).project('bytes')['bytes'][0]
    mem_bytes_fix = db.scan(ar_name).fetch(atts_only=True,
                                           as_dataframe=False).nbytes
    db.aio_save(
        ar_name, "'/dev/shm/{}'".format(ar_name), "'format=arrow'").fetch()
    file_bytes_fix = os.path.getsize('/dev/shm/' + ar_name)

    ar_name = ar_names[1]
    db.build('<z:int64 not null>[i=1:{}]'.format(cnt),
             'random()'
             ).apply('y', 'int64(random())',
                     'x', 'int64(random())',
                     'w', 'int64(random())',
                     'v', "''"
                     ).store(ar_name)
    ar_schemas[1] = db.arrays[ar_name].schema()
    scidb_bytes_var = db.summarize(ar_name).project('bytes')['bytes'][0]
    mem_bytes_var = db.scan(ar_name).fetch(atts_only=True,
                                           as_dataframe=False).nbytes
    db.aio_save(
        ar_name, "'/dev/shm/{}'".format(ar_name), "'format=arrow'").fetch()
    file_bytes_var = os.path.getsize('/dev/shm/' + ar_name)

    print("""
Setup
===
Target size:    {:6.2f} MB
Number of runs: {:3d}

Fix Size Schema (int64 only)
---
SciDB size:     {:6.2f} MB
In-memory size: {:6.2f} MB
File size:      {:6.2f} MB

Variable Size Schema (int64 and string)
---
SciDB size:     {:6.2f} MB
In-memory size: {:6.2f} MB
File size:      {:6.2f} MB""".format(
    mb,
    runs,
    scidb_bytes_fix / 1024. / 1024,
    mem_bytes_fix / 1024. / 1024,
    file_bytes_fix / 1024. / 1024,
    scidb_bytes_var / 1024. / 1024,
    mem_bytes_var / 1024. / 1024,
    file_bytes_var / 1024. / 1024))

    return db


def save(mb, runs):
    setup = """
import scidbpy

db = scidbpy.connect()"""

    ar_name = ar_names[0]
    print("""
Save
===
Fix Size Schema (int64 only)
---""")
    fmt = '(int64,int64,int64,int64)'
    stmt = """
db.iquery("aio_save({ar_name}, '/dev/shm/{ar_name}', 'format={fmt}')",
          fetch = True)""".format(
              ar_name=ar_name,
              fmt=fmt)
    rt = timeit.Timer(stmt=stmt, setup=setup).timeit(number=runs) / runs
    print("""\
Binary: {:6.2f} seconds {:6.2f} MB/second""".format(
      rt, mb / rt))

    fmt = 'arrow'
    stmt = """
db.iquery("aio_save({ar_name}, '/dev/shm/{ar_name}', 'format={fmt}')",
          fetch = True)""".format(
              ar_name=ar_name,
              fmt=fmt)
    rt = timeit.Timer(stmt=stmt, setup=setup).timeit(number=runs) / runs
    print("""\
Arrow:  {:6.2f} seconds {:6.2f} MB/second""".format(
      rt, mb / rt))

    ar_name = ar_names[1]
    print("""
Variable Size Schema (int64 and string)
---""")
    fmt = '(int64,int64,int64,int64,string)'
    stmt = """
db.iquery("aio_save({ar_name}, '/dev/shm/{ar_name}', 'format={fmt}')",
          fetch = True)""".format(
              ar_name=ar_name,
              fmt=fmt)
    rt = timeit.Timer(stmt=stmt, setup=setup).timeit(number=runs) / runs
    print("""\
Binary: {:6.2f} seconds {:6.2f} MB/second""".format(
      rt, mb / rt))

    fmt = 'arrow'
    stmt = """
db.iquery("aio_save({ar_name}, '/dev/shm/{ar_name}', 'format={fmt}')",
          fetch = True)""".format(
              ar_name=ar_name,
              fmt=fmt)
    rt = timeit.Timer(stmt=stmt, setup=setup).timeit(number=runs) / runs
    print("""\
Arrow:  {:6.2f} seconds {:6.2f} MB/second""".format(
      rt, mb / rt))


def download(mb, runs):
    setup = """
import scidbpy

db = scidbpy.connect()
schemas = (scidbpy.schema.Schema.fromstring('{}'),
           scidbpy.schema.Schema.fromstring('{}'))""".format(
               *ar_schemas)

    i = 0
    print("""
Download
===
Fix Size Schema (int64 only)
---""")
    stmt = """
db.iquery('scan({})',
          fetch=True,
          atts_only=True,
          schema=schemas[{}])""".format(ar_names[i], i)
    rt = timeit.Timer(stmt=stmt, setup=setup).timeit(number=runs) / runs
    print("""\
Binary: {:6.2f} seconds {:6.2f} MB/second""".format(
      rt, mb / rt))

    fmt = 'arrow'
    stmt = """
db.iquery('scan({})',
          fetch=True,
          use_arrow=True,
          atts_only=True,
          schema=schemas[{}])""".format(ar_names[i], i)
    rt = timeit.Timer(stmt=stmt, setup=setup).timeit(number=runs) / runs
    print("""\
Arrow:  {:6.2f} seconds {:6.2f} MB/second""".format(
      rt, mb / rt))

    i = 1
    print("""
Variable Size Schema (int64 and string)
---""")
    stmt = """
db.iquery('scan({})',
          fetch=True,
          atts_only=True,
          schema=schemas[{}])""".format(ar_names[i], i)
    rt = timeit.Timer(stmt=stmt, setup=setup).timeit(number=runs) / runs
    print("""\
Binary: {:6.2f} seconds {:6.2f} MB/second""".format(
      rt, mb / rt))

    fmt = 'arrow'
    stmt = """
db.iquery('scan({})',
          fetch=True,
          use_arrow=True,
          atts_only=True,
          schema=schemas[{}])""".format(ar_names[i], i)
    rt = timeit.Timer(stmt=stmt, setup=setup).timeit(number=runs) / runs
    print("""\
Arrow:  {:6.2f} seconds {:6.2f} MB/second""".format(
      rt, mb / rt))


def cleanup(db):
    for ar_name in ar_names:
        db.remove(ar_name)


if __name__ == "__main__":
    try:
        mb = int(sys.argv[1])
    except Exception:
        mb = 5                      # MB
    runs = 3

    db = setup(mb)

    save(mb, runs)
    download(mb, runs)

    cleanup(db)