#!/bin/bash

set -e

while getopts "l:g:d:o:" opt; do
  case "$opt" in
    l)  llvm_dir=$OPTARG
        ;;
    g)  gcov_dir=$OPTARG
        ;;
    d)  klee_out=$OPTARG
        ;;
    o)  output_dir=$OPTARG
        ;;
    esac
done
shift $(($OPTIND - 1));

mkdir -p $output_dir
curr=$(pwd)
echo "tool,optimization,coverage" > $output_dir/metrics.csv
touch $output_dir/warnings.txt
truncate -s 0 $output_dir/warnings.txt

for d in $klee_out/*; do
  tool=$(basename $d | cut -d '-' -f 1)
  opt=$(basename $d | cut -d '-' -f 2)
  # We remove already tracked coverage.
  if [ -fe $gcov_dir/src/$tool.gcda ]; then
    rm $gcov_dir/src/$tool.gcda
  fi

  for f in $d/*.ktest; do
    set +e
    klee-replay $gcov_dir/src/$tool $f
    rc=$?
    set -e
    if [ $rc -ne 0 ]; then
      echo "Warning with: $f" >> $output_dir/warnings.txt
    fi
  done
  cd $gcov_dir/src/
  coverage=$(gcov $tool | grep -oE "[0-9]+\.[0-9]+" | head -1)
  cd $curr
  echo "$tool,$opt,$coverage" >> $output_dir/metrics.csv
done
