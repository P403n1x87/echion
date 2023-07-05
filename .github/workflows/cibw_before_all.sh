#!/bin/bash

# Install libunwind
git clone https://github.com/libunwind/libunwind
cd libunwind
autoreconf -i
./configure CFLAGS='-fPIC' CXXFLAGS='-fPIC'
make
make install
cd ..
rm -rf libunwind

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
