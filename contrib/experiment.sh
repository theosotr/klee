#!/bin/bash

tools=(
  base64
  basename
  chcon
  cksum
  comm
  cut
  dd
  dircolors
  dirname
  du
  env
  expand
  expr
  fold
  groups
  link
  logname
  mkdir
  mkfifo
  mknod
  nice
  nl
  od
  paste
  pathchk
  printf
  readlink
  rmdir
  setuidgid
  sleep
  split
  sum
  sync
  tee
  touch
  tr
  tsort
  unexpand
  unlink
  wc
)

optimizations=(
  all
  adce
  argpromotion
  cfgsimpl
  constmerge
  dae
  dse
  funcattrs
  inline
  gdce
  gopt
  gvn
  indvarsimpl
  instrcomb
  ipconstprop
  jmpthreading
  licm
  loopdel
  looprotate
  loopunroll
  loopunswitch
  memcpyopt
  memtoreg
  pruneesh
  reassoc
  sreplaggr
  sccp
  stripdp
  tailce
)


while getopts "s:t:o:l:" opt; do
  case "$opt" in
    s)  solver_backend=$OPTARG
        ;;
    t)  timeout=$OPTARG
        ;;
    o)  output_dir=$OPTARG
        ;;
    l)  llvm_dir=$OPTARG
        ;;
    esac
done
shift $(($OPTIND - 1));

mkdir -p $output_dir/klee-data

for tool in "${tools[@]}"; do 
  for opt in "${optimizations[@]}"; do

    toolbc=$llvm_dir/src/$tool.bc
    if [ "$opt" = all ]; then
	    klee --simplify-sym-indices \
	      --write-cov \
        --use-query-log solver:kquery \
        --write-kqueries \
	      --output-module \
	      --allow-external-sym-calls \
	      --only-output-states-covering-new \
	      --output-dir="$output_dir/$tool-$opt-$timeout" \
	      --max-sym-array-size=4096 \
	      --max-instruction-time=10. \
	      --max-time=$timeout \
	      --watchdog \
	      --search dfs \
	      --max-static-fork-pct=1 \
	      --max-static-solve-pct=1 \
	      --max-static-cpfork-pct=1 \
	      --posix-runtime \
	      --libc=uclibc \
        --solver-backend=$solver_backend \
        --optimize \
	      --use-cache=0 \
	      --use-cex-cache=0 \
	      $toolbc --sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8
    else
	    klee --simplify-sym-indices \
	      --write-cov \
        --use-query-log solver:kquery \
        --write-kqueries \
	      --output-module \
	      --allow-external-sym-calls \
	      --only-output-states-covering-new \
	      --output-dir="$output_dir/$tool-$opt-$timeout" \
	      --max-sym-array-size=4096 \
	      --max-instruction-time=10. \
	      --max-time=$timeout \
	      --watchdog \
	      --search dfs \
	      --max-static-fork-pct=1 \
	      --max-static-solve-pct=1 \
	      --max-static-cpfork-pct=1 \
	      --posix-runtime \
	      --libc=uclibc \
        --solver-backend=$solver_backend \
	      --optimize \
	      --opt-type=$opt \
	      --use-cache=0 \
	      --use-cex-cache=0 \
	      $toolbc --sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8
    fi

  done
  klee --simplify-sym-indices \
    --write-cov \
    --use-query-log solver:kquery \
    --write-kqueries \
    --output-module \
    --allow-external-sym-calls \
    --only-output-states-covering-new \
    --output-dir="$output_dir/$tool-no-$timeout" \
    --max-sym-array-size=4096 \
    --max-instruction-time=10. \
    --max-time=$timeout \
    --watchdog \
    --search dfs \
    --max-static-fork-pct=1 \
    --max-static-solve-pct=1 \
    --max-static-cpfork-pct=1 \
    --posix-runtime \
    --libc=uclibc \
    --solver-backend=$solver_backend \
    --use-cache=0 \
    --use-cex-cache=0 \
    $toolbc --sym-args 0 1 10 --sym-args 0 2 2 --sym-files 1 8 --sym-stdin 8
done
