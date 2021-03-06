//===-- Optimize.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#define DEFAULT_OPT_PASSES 36


#include <map>

#include "llvm/Pass.h"


using namespace llvm;


class Optimization {
public:
  enum StdOptimization {
    AGGRESSIVE_DCE,
    ALWAYS_INLINE,
    ARG_PROMOTION,
    CFG_SIMPL,
    CONST_MERGE,
    CVP,
    DEAD_ARG_ELIM,
    DEAD_STORE_ELIM,
    EARLY_CSE,
    FUNC_ATTRS,
    FUNC_INLINING,
    GLOBAL_DCE,
    GLOBAL_OPT,
    GLOBALS_MODREF,
    GVN,
    IND_VAR_SIMPL,
    INSTR_COMBINING,
    INTERNALIZE,
    IP_CONST_PROP,
    IPSCCP,
    JMP_THREADING,
    LAZY_VALUE_INFO,
    LCSSA,
    LICM,
    LOOP_DELETION,
    LOOP_IDIOM,
    LOOP_ROTATE,
    LOOP_UNROLL,
    LOOP_UNSWITCH,
    LOOP_SIMPL,
    LOWER_EXPECT,
    MEMCPY_OPT,
    MEM_TO_REG,
    PRUNE_EH,
    REASSOC,
    SCALAR_REPL_AGGR,
    SCCP,
    STRIP_DEAD_PROTOTYPE,
    TAIL_CALL_ELIM,
  };

public:
  Pass *GenerateOptPass(enum StdOptimization opt);

};

#endif
