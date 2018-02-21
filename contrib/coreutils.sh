#!/bin/bash

set -e

COREUTILS_VERSION=6.11
GCOV_DIR=gcov-obj
LLVM_DIR=llvm-obj
LLVM_COMPILER=clang

wget https://ftp.gnu.org/gnu/coreutils/coreutils-${COREUTILS_VERSION}.tar.gz
tar -xvzf coreutils-${COREUTILS_VERSION}.tar.gz
cd coreutils-${COREUTILS_VERSION}
mkdir $GCOV_DIR
cd $GCOV_DIR
../configure --disable-nls CFLAGS="-g -fprofile-arcs -ftest-coverage"
make
make -C src arch hostname

cd ..

mkdir $LLVM_DIR
cd $LLVM_DIR
CC=wllvm ../configure --disable-nls CFLAGS="-g"
CC=wllvm make
CC=wllvm make -C src arch hostname
find . -executable -type f | xargs -I '{}' extract-bc '{}'
