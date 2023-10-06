#!/bin/bash
LIBDD_VERSION=v3.0.0
LIBDD_NAME=libdatadog-x86_64-unknown-linux-gnu.tar.gz
LIBDD_DIR=${LIBDD_NAME%.tar.gz}
DD_ROOT=vendored/dd-trace-py/ddtrace/internal/datadog

# get libdatadog 3.0.0 on Linux x86_64
if [ ! -d $DD_ROOT/libdatadog ]; then
  curl -LO https://github.com/DataDog/libdatadog/releases/download/v3.0.0/$LIBDD_NAME \
    && tar -xvf $LIBDD_NAME \
    && rm -f $LIBDD_NAME \
    && mv $LIBDD_DIR $DD_ROOT/libdatadog
  
  if [ $? -ne 0 ]; then
    echo "Something happened setting up libdatadog, please check"
    exit 1
  fi
else
  echo "libdatadog directory exists, not downloading"
fi

# Build ddup into a .so
mkdir -p build/ddup
g++ -std=c++17 -O3 -fPIC -c \
  -I$DD_ROOT/libdatadog/include/ \
  -I$DD_ROOT/profiling/include/ \
  $DD_ROOT/profiling/src/exporter.cpp \
  -o build/ddup/exporter.o

g++ -std=c++17 -O3 -fPIC -c \
  -I$DD_ROOT/libdatadog/include/ \
  -I$DD_ROOT/profiling/include/ \
  $DD_ROOT/profiling/src/interface.cpp \
  -o build/ddup/interface.o

if [ $? -ne 0 ]; then
  echo "Something happened compiling ddup"
  exit 1
fi

# Build into .a
ar rcs libuploader.a build/ddup/*.o
