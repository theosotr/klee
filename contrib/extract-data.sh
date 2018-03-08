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
echo "tool,optimization,coverage,exec_time,solver_time,nu_queries,query_con" > $output_dir/metrics.csv
touch $output_dir/warnings.txt
truncate -s 0 $output_dir/warnings.txt

for d in $klee_out/*; do
  tool=$(basename $d | cut -d '-' -f 1)
  opt=$(basename $d | cut -d '-' -f 2)
  # We remove already tracked coverage.
  rm $gcov_dir/src/$tool.gcda -f

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

  klee_stats=$(klee-stats basename-all-300/ --print-all | \
               sed -e '1,3d;$d' | \
               awk -F "\|" '/[\|].*[0-9]+\.[0-9]+/ {print $4, $8, $15, $16}')
  read exec_time solver_time nu_queries query_con <<< $(echo $klee_stats)
  echo "$tool,$opt,$coverage,$exec_time,$solver_time,$nu_queries,$query_con" >> $output_dir/metrics.csv
done
