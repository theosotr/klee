// FIXME: This file is a bastard child of opt.cpp and llvm-ld's
// Optimize.cpp. This stuff should live in common code.


//===- Optimize.cpp - Optimize a complete program -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements all optimization of the linked module for llvm-ld.
//
//===----------------------------------------------------------------------===//
#include <vector>

#include "Optimize.h"

#include "klee/Config/Version.h"
#include "klee/Internal/Module/LLVMPassManager.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 5)
#include "llvm/IR/Verifier.h"
#else
#include "llvm/Analysis/Verifier.h"
#endif

using namespace llvm;

// Don't verify at the end
static cl::opt<bool> DontVerify("disable-verify", cl::ReallyHidden);

static cl::opt<bool> DisableInline("disable-inlining",
  cl::desc("Do not run the inliner pass"));

static cl::opt<bool>
DisableOptimizations("disable-opt",
  cl::desc("Do not run any optimization passes"));

static cl::opt<bool> DisableInternalize("disable-internalize",
  cl::desc("Do not mark all symbols as internal"));

static cl::opt<bool> VerifyEach("verify-each",
 cl::desc("Verify intermediate results of all passes"));

static cl::alias ExportDynamic("export-dynamic",
  cl::aliasopt(DisableInternalize),
  cl::desc("Alias for -disable-internalize"));

static cl::opt<bool> Strip("strip-all", 
  cl::desc("Strip all symbol info from executable"));

static cl::alias A0("s", cl::desc("Alias for --strip-all"), 
  cl::aliasopt(Strip));

static cl::opt<bool> StripDebug("strip-debug",
  cl::desc("Strip debugger symbol info from executable"));

static cl::alias A1("S", cl::desc("Alias for --strip-debug"),
  cl::aliasopt(StripDebug));

static cl::list<Optimization::StdOptimization>
  OptType("opt-type",
      cl::desc("Select optimization to be applied before execution.  (default=off)"),
      cl::values(
                 clEnumValN(Optimization::AGGRESSIVE_DCE, "adce", "Delete dead instructions"),
                 clEnumValN(Optimization::ARG_PROMOTION, "argpromotion", "Scalarize uninlined fn args"),
                 clEnumValN(Optimization::CFG_SIMPL, "cfgsimpl", "Clean up disgusting code"),
                 clEnumValN(Optimization::CONST_MERGE, "constmerge", "Merge dup global constants"),
                 clEnumValN(Optimization::DEAD_ARG_ELIM, "dae", "Dead argument elimination"),
                 clEnumValN(Optimization::DEAD_STORE_ELIM, "dse", "Dead store elimination"),
                 clEnumValN(Optimization::FUNC_ATTRS, "funcattrs", "Deduce function attributes"),
                 clEnumValN(Optimization::FUNC_INLINING, "inline", "Inline small functions"),
                 clEnumValN(Optimization::GLOBAL_DCE, "gdce", "Remove unused functions and globals"), 
                 clEnumValN(Optimization::GLOBAL_OPT, "gopt", "Optimize global variables"),
                 clEnumValN(Optimization::GLOBALS_MODREF, "globmodref", "Perform IP alias analysis"),
                 clEnumValN(Optimization::GVN, "gvn", "Remove redundancies"),
                 clEnumValN(Optimization::IND_VAR_SIMPL, "indvarsimpl", "Canonicalize induction variables"),
                 clEnumValN(Optimization::INSTR_COMBINING, "instrcomb", "Combine two instructions into one instruction"),
                 clEnumValN(Optimization::INTERNALIZE, "internalize", "Make all functions internal except for main"),
                 clEnumValN(Optimization::IP_CONST_PROP, "ipconstprop", "Perform constant propagation"),
                 clEnumValN(Optimization::IPSCCP, "ipsccp", "Perform an interprocedural SCCP"),
                 clEnumValN(Optimization::JMP_THREADING, "jmpthreading", "Perform jump threading"),
                 clEnumValN(Optimization::LICM, "licm", "Hoist loop invariants"),
                 clEnumValN(Optimization::LOOP_DELETION, "loopdel", "Delete dead loops"),
                 clEnumValN(Optimization::LOOP_ROTATE, "looprotate", "Rotate loops"),
                 clEnumValN(Optimization::LOOP_UNROLL, "loopunroll", "Reduce the number of iterations by unrolling loops"),
                 clEnumValN(Optimization::LOOP_UNSWITCH,"loopunswitch", "Unswitch loops"),
                 clEnumValN(Optimization::MEMCPY_OPT, "memcpyopt", "Remove memcpy / form memset"),
                 clEnumValN(Optimization::MEM_TO_REG, "memtoreg", "Kill useless allocas"),
                 clEnumValN(Optimization::PRUNE_EH, "pruneeh", "Remove unused exception handling info"),
                 clEnumValN(Optimization::REASSOC, "reassoc", "Reassociate expressions"),
                 clEnumValN(Optimization::SCALAR_REPL_AGGR, "sreplaggr", "Break up aggregated allocas"),
                 clEnumValN(Optimization::SCCP, "sccp", "Perform constant propagation with SCCP"),
                 clEnumValN(Optimization::STRIP_DEAD_PROTOTYPE, "stripdp", "Get rid of dead prototypes"),
                 clEnumValN(Optimization::TAIL_CALL_ELIM, "tailce", "Eliminate tail calls")
		    KLEE_LLVM_CL_VAL_END),
		  cl::CommaSeparated);


// A utility function that adds a pass to the pass manager but will also add
// a verifier pass after if we're supposed to verify.
static inline void addPass(klee::LegacyLLVMPassManagerTy &PM, Pass *P) {
  // Add the pass to the pass manager...
  PM.add(P);

  // If we are verifying all of the intermediate steps, add the verifier...
  if (VerifyEach)
    PM.add(createVerifierPass());
}


static Pass *GenerateOptPass(enum Optimization::StdOptimization StdOpt,
                             const std::string &EntryPoint) {
  switch (StdOpt) {
    case Optimization::AGGRESSIVE_DCE:
      return createAggressiveDCEPass();
    case Optimization::ARG_PROMOTION:
      return createArgumentPromotionPass();
    case Optimization::CFG_SIMPL:
      return createCFGSimplificationPass();
    case Optimization::CONST_MERGE:
      return createConstantMergePass();
    case Optimization::DEAD_ARG_ELIM:
      return createDeadArgEliminationPass();
    case Optimization::DEAD_STORE_ELIM:
      return createDeadStoreEliminationPass();
    case Optimization::FUNC_ATTRS:
      return createFunctionAttrsPass();
    case Optimization::FUNC_INLINING:
      return DisableInline ? NULL: createFunctionInliningPass();
    case Optimization::GLOBAL_DCE:
      return createGlobalDCEPass();
    case Optimization::GLOBAL_OPT:
      return createGlobalOptimizerPass();
    case Optimization::GLOBALS_MODREF:
      return createGlobalsModRefPass();
    case Optimization::GVN:
      return createGVNPass();
    case Optimization::IND_VAR_SIMPL:
      return createIndVarSimplifyPass();
    case Optimization::INSTR_COMBINING:
      return createInstructionCombiningPass();
    case Optimization::INTERNALIZE:
      return DisableInternalize ? NULL:
        createInternalizePass(
            std::vector<const char *>(1, EntryPoint.c_str()));
    case Optimization::IP_CONST_PROP:
      return createIPConstantPropagationPass();
    case Optimization::IPSCCP:
      return createIPSCCPPass();
    case Optimization::JMP_THREADING:
      return createJumpThreadingPass();
    case Optimization::LICM:
      return createLICMPass();
    case Optimization::LOOP_DELETION:
      return createLoopDeletionPass();
    case Optimization::LOOP_ROTATE:
      return createLoopRotatePass();
    case Optimization::LOOP_UNROLL:
      return createLoopUnrollPass();
    case Optimization::LOOP_UNSWITCH:
      return createLoopUnswitchPass();
    case Optimization::MEMCPY_OPT:
      return createMemCpyOptPass();
    case Optimization::MEM_TO_REG:
      return createPromoteMemoryToRegisterPass();
    case Optimization::PRUNE_EH:
      return createPruneEHPass();
    case Optimization::REASSOC:
      return createReassociatePass();
    case Optimization::SCALAR_REPL_AGGR:
      return createScalarReplAggregatesPass();
    case Optimization::SCCP:
      return createSCCPPass();
    case Optimization::STRIP_DEAD_PROTOTYPE:
      return createStripDeadPrototypesPass();
    default:
      // This is Tail Call Elimination optimization.
      return createTailCallEliminationPass();
  }
}


namespace llvm {

static std::vector<Optimization::StdOptimization> DefaultStdOptimizations = {
  Optimization::CFG_SIMPL,
  Optimization::MEM_TO_REG,
  Optimization::GLOBAL_OPT,
  Optimization::MEM_TO_REG,
  Optimization::IP_CONST_PROP,
  Optimization::DEAD_ARG_ELIM,
  Optimization::INSTR_COMBINING,
  Optimization::CFG_SIMPL,
  Optimization::PRUNE_EH,
  Optimization::FUNC_ATTRS,
  Optimization::FUNC_INLINING,
  Optimization::ARG_PROMOTION,
  Optimization::INSTR_COMBINING,
  Optimization::JMP_THREADING,
  Optimization::CFG_SIMPL,
  Optimization::SCALAR_REPL_AGGR,
  Optimization::INSTR_COMBINING,
  Optimization::TAIL_CALL_ELIM,
  Optimization::CFG_SIMPL,
  Optimization::REASSOC,
  Optimization::LOOP_ROTATE,
  Optimization::LICM,
  Optimization::LOOP_UNSWITCH,
  Optimization::INSTR_COMBINING,
  Optimization::IND_VAR_SIMPL,
  Optimization::LOOP_DELETION,
  Optimization::LOOP_UNROLL,
  Optimization::INSTR_COMBINING,
  Optimization::GVN,
  Optimization::MEMCPY_OPT,
  Optimization::SCCP,
  Optimization::INSTR_COMBINING,
  Optimization::DEAD_STORE_ELIM,
  Optimization::AGGRESSIVE_DCE,
  Optimization::STRIP_DEAD_PROTOTYPE,
  Optimization::CONST_MERGE,

  // Now that composite has been compiled, scan through the module, looking
  // for a main function.  If main is defined, mark all other functions
  // internal.
  Optimization::INTERNALIZE,

  // Propagate constants at call sites into the functions they call.  This
  // opens opportunities for globalopt (and inlining) by substituting function
  // pointers passed as arguments to direct uses of functions.
  Optimization::IPSCCP,

  // Now that we internalized some globals, see if we can hack on them!
  Optimization::GLOBAL_OPT,

  // Linking modules together can lead to duplicated global constants, only
  // keep one copy of each constant...
  Optimization::CONST_MERGE,

  // Remove unused arguments from functions...
  Optimization::DEAD_ARG_ELIM,

  // Reduce the code after globalopt and ipsccp.  Both can open up significant
  // simplification opportunities, and both can propagate functions through
  // function pointers.  When this happens, we often have to resolve varargs
  // calls, etc, so let instcombine do this.
  Optimization::INSTR_COMBINING,

  // Inline small functions.
  Optimization::FUNC_INLINING,

  // Remove dead EH info.
  Optimization::PRUNE_EH,

  // Optimize globals again.
  Optimization::GLOBAL_OPT,

  // Remove dead functions.
  Optimization::GLOBAL_DCE,

  // If we didn't decide to inline a function, check to see if we can
  // transform it to pass arguments by value instead of by reference.
  Optimization::ARG_PROMOTION,

  // The IPO passes may leave cruft around.  Clean up after them.
  Optimization::INSTR_COMBINING,
  Optimization::JMP_THREADING,
  Optimization::SCALAR_REPL_AGGR,

  // Run a few AA driven optimizations here and now, to cleanup the code.
  Optimization::FUNC_ATTRS,
  Optimization::GLOBALS_MODREF,
  Optimization::LICM,
  Optimization::GVN,
  Optimization::MEMCPY_OPT,
  Optimization::DEAD_STORE_ELIM,

  // Cleanup and simplify the code after the scalar optimizations.
  Optimization::INSTR_COMBINING,
  Optimization::JMP_THREADING,
  Optimization::MEM_TO_REG,

  // Delete basic blocks, which optimization passes may have killed...
  Optimization::CFG_SIMPL,

  // Now that we have optimized the program, discard unreachable functions...
  Optimization::GLOBAL_DCE,
  Optimization::INSTR_COMBINING,
  Optimization::CFG_SIMPL,
  Optimization::AGGRESSIVE_DCE,
  Optimization::GLOBAL_DCE
};

static void AddStandardCompilePasses(klee::LegacyLLVMPassManagerTy &PM,
                                     const std::string &EntryPoint) {
  PM.add(createVerifierPass());                  // Verify that input is correct

  // If the -strip-debug command line option was specified, do it.
  if (StripDebug)
    addPass(PM, createStripSymbolsPass(true));

  if (DisableOptimizations) return;

  std::vector<enum Optimization::StdOptimization> opts = (
      OptType.size() ? OptType: DefaultStdOptimizations); 

  for (unsigned i = 0; i < opts.size(); i++) {
    Pass *optPass = GenerateOptPass(opts[i], EntryPoint);
    if (optPass)
      addPass(PM, optPass);
  }
}

/// Optimize - Perform link time optimizations. This will run the scalar
/// optimizations, any loaded plugin-optimization modules, and then the
/// inter-procedural optimizations if applicable.
void Optimize(Module *M, const std::string &EntryPoint) {

  // Instantiate the pass manager to organize the passes.
  klee::LegacyLLVMPassManagerTy Passes;

  // If we're verifying, start off with a verification pass.
  if (VerifyEach)
    Passes.add(createVerifierPass());

  // Add an appropriate DataLayout instance for this module...
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 6)
  DataLayoutPass *dlpass = new DataLayoutPass();
  dlpass->doInitialization(*M);
  addPass(Passes, dlpass);
#elif LLVM_VERSION_CODE >= LLVM_VERSION(3, 5)
  addPass(Passes, new DataLayoutPass(M));
#else
  addPass(Passes, new DataLayout(M));
#endif

  // DWD - Run the opt standard pass list as well.
  AddStandardCompilePasses(Passes, EntryPoint);

  // If the -s or -S command line options were specified, strip the symbols out
  // of the resulting program to make it smaller.  -s and -S are GNU ld options
  // that we are supporting; they alias -strip-all and -strip-debug.
  if (Strip || StripDebug)
    addPass(Passes, createStripSymbolsPass(StripDebug && !Strip));

  // Make sure everything is still good.
  if (!DontVerify)
    Passes.add(createVerifierPass());

  // Run our queue of passes all at once now, efficiently.
  Passes.run(*M);
}

}
