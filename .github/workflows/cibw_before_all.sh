#!/bin/bash

set -ex

# on musllinux we need to install libtool
if ldd --version 2>&1 | grep -q musl; then
    apk add libtool po4a
fi

. scripts/build_libunwind.sh

sed -i 's/mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-*
sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-*

# Install xz
yum makecache
yum install -y gettext-devel

git clone https://git.tukaani.org/xz.git
cd xz
./autogen.sh
./configure CFLAGS='-fPIC' CXXFLAGS='-fPIC'
make
make install
cd ..
rm -rf xz
