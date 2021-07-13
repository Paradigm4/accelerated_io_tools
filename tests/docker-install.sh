#!/bin/sh

set -o errexit

wget -O- https://paradigm4.github.io/extra-scidb-libs/install.sh \
|  sh -s -- --only-prereq

id=`lsb_release --id --short`
BASEDIR=$(dirname $0)/..

# Install prerequisites
PYTHON=python3
if [ "$id" = "CentOS" ]
then
    for pkg in centos-release-scl libpqxx-devel python3 cmake3
    do
        yum install --assumeyes $pkg
    done

    # Compile and Install Apache Arrow from Source
    curl --location \
        "https://www.apache.org/dyn/closer.lua?action=download&filename=arrow/arrow-3.0.0/apache-arrow-3.0.0.tar.gz" \
        | tar --extract --gzip --directory=$BASEDIR
    old_path=`pwd`
    cd $BASEDIR/apache-arrow-3.0.0/cpp
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
    cd ..
    rm -rf build
    cd $old_path
else
    apt-get install --assume-yes --no-install-recommends        \
            software-properties-common
    add-apt-repository --yes ppa:deadsnakes/ppa
    apt-get update
    apt-get install --assume-yes --no-install-recommends        \
            libarrow-dev=$ARROW_VER-1                           \
            libpqxx-dev                                         \
            python3.7
    PYTHON=python3.7
fi


# Setup Python
wget --no-verbose https://bootstrap.pypa.io/get-pip.py
$PYTHON get-pip.py
pip install --upgrade scidb-py


# Reset SciDB instance count to 4
scidbctl.py stop $SCIDB_NAME
rm --recursive $SCIDB_INSTALL_PATH/DB-$SCIDB_NAME
sed --in-place s/server-0=127.0.0.1,1/server-0=127.0.0.1,3/ \
    $SCIDB_INSTALL_PATH/etc/config.ini
scidbctl.py init-cluster --force $SCIDB_NAME
scidbctl.py start $SCIDB_NAME
iquery --afl --query "list('instances')"


# Turn on aio in Shim
echo aio=1 > /var/lib/shim/conf
if [ "$id" = "CentOS" ]
then
    systemctl restart shimsvc
else
    service shimsvc restart
fi


# Compile and install plugin
scidbctl.py stop $SCIDB_NAME
make --directory=$BASEDIR
cp $BASEDIR/libaccelerated_io_tools.so $SCIDB_INSTALL_PATH/lib/scidb/plugins/
scidbctl.py start $SCIDB_NAME
iquery --afl --query "load_library('accelerated_io_tools')"
