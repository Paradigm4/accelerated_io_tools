#!/bin/sh

set -o errexit

wget -O- https://paradigm4.github.io/extra-scidb-libs/install.sh \
|  sh -s -- --only-prereq

id=`lsb_release --id --short`

# Install prerequisites
if [ "$id" = "CentOS" ]
then
    # yum install --assumeyes                                                    \
    #     https://dl.fedoraproject.org/pub/epel/epel-release-latest-6.noarch.rpm

    for pkg in arrow-devel-$ARROW_VER centos-release-scl libpqxx-devel python27
    do
        yum install --assumeyes $pkg
    done

    source /opt/rh/python27/enable
else
    apt-get install --assume-yes --no-install-recommends        \
            software-properties-common
    add-apt-repository --yes ppa:deadsnakes/ppa
    apt-get update
    apt-get install --assume-yes --no-install-recommends        \
            libarrow-dev=$ARROW_VER-1                           \
            libpqxx-dev                                         \
            python3.7
fi


wget --no-verbose https://bootstrap.pypa.io/get-pip.py
python3.7 get-pip.py
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
make --directory accelerated_io_tools
cp accelerated_io_tools/libaccelerated_io_tools.so     \
   $SCIDB_INSTALL_PATH/lib/scidb/plugins/
scidbctl.py start $SCIDB_NAME
iquery --afl --query "load_library('accelerated_io_tools')"
