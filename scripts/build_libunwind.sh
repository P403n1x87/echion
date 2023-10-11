#/bin/bash

# Install libunwind
git clone https://github.com/libunwind/libunwind
cd libunwind
autoreconf -i
./configure CFLAGS='-fPIC' CXXFLAGS='-fPIC'
make -j8
make install
cd ..
rm -rf libunwind
