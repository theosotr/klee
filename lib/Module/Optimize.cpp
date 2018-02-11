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
                 clEnumValN(Optimization::AGGRESSIVE_DCE, "aggressive_dce", "Delete dead instructions"),
                 clEnumValN(Optimization::ARG_PROMOTION, "arg_promotion", "Scalarize uninlined fn args"),
                 clEnumValN(Optimization::CFG_SIMPL, "cfg_simpl", "Clean up disgusting code"),
                 clEnumValN(Optimization::CONST_MERGE, "const_merge", "Merge dup global constants"),
                 clEnumValN(Optimization::DEAD_ARG_ELIM, "dead_arg_elim", "Dead argument elimination"),
                 clEnumValN(Optimization::DEAD_STORE_ELIM, "dead_store_elim", "Dead store elimination"),
                 clEnumValN(Optimization::FUNC_ATTRS, "func_attrs", "Deduce function attributes"),
                 clEnumValN(Optimization::FUNC_INLINING, "func_inilining", "Inline small functions"),
                 clEnumValN(Optimization::GLOBAL_DCE, "global_dce", "Remove unused functions and globals"), 
                 clEnumValN(Optimization::GLOBAL_OPT, "global_opt", "Optimize global variables"),
                 clEnumValN(Optimization::GVN, "gvn", "Remove redundancies"),
                 clEnumValN(Optimization::IND_VAR_SIMPL, "ind_val_simpl", "Canonicalize induction variables"),
                 clEnumValN(Optimization::INSTR_COMBINING, "instr_combining", "Combine two instructions into one instruction"),
                 clEnumValN(Optimization::IP_CONST_PROP, "ip_const_prop", "Perform constant propagation"),
                 clEnumValN(Optimization::JMP_THREADING, "jmp_threading", "Perform jump threading"),
                 clEnumValN(Optimization::LICM, "licm", "Hoist loop invariants"),
                 clEnumValN(Optimization::LOOP_DELETION, "loop_deletion", "Delete dead loops"),
                 clEnumValN(Optimization::LOOP_ROTATE, "loop_rotate", "Rotate loops"),
                 clEnumValN(Optimization::LOOP_UNROLL, "loop_unroll", "Reduce the number of iterations by unrolling loops"),
                 clEnumValN(Optimization::LOOP_UNSWITCH,"loop_unswitch", "Unswitch loops"),
                 clEnumValN(Optimization::MEMCPY_OPT, "memcpy_opt", "Remove memcpy / form memset"),
                 clEnumValN(Optimization::MEM_TO_REG, "mem_to_reg", "Kill useless allocations"),
                 clEnumValN(Optimization::PRUNE_EH, "prune_eh", "Remove unused exception handling info"),
                 clEnumValN(Optimization::REASSOC, "reassoc", "Reassociate expressions"),
                 clEnumValN(Optimization::SCALAR_REPL_AGGR, "scalar_repl_aggr", "Break up aggregated allocations"),
                 clEnumValN(Optimization::SCCP, "sccp", "Perform constant propagation with SCCP"),
                 clEnumValN(Optimization::STRIP_DEAD_PROTOTYPE, "strip_dead_prototype", "Get rid of dead prototypes"),
                 clEnumValN(Optimization::TAIL_CALL_ELIM, "tail_call_elim", "Eliminate tail calls")
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


static Pass *GenerateOptPass(enum Optimization::StdOptimization stdOpt) {
  switch (stdOpt) {
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
      return DisableInline ? createFunctionInliningPass(): NULL;
    case Optimization::GLOBAL_DCE:
      return createGlobalDCEPass();
    case Optimization::GLOBAL_OPT:
      return createGlobalOptimizerPass();
    case Optimization::GVN:
      return createGVNPass();
    case Optimization::IND_VAR_SIMPL:
      return createIndVarSimplifyPass();
    case Optimization::INSTR_COMBINING:
      return createInstructionCombiningPass();
    case Optimization::IP_CONST_PROP:
      return createIPConstantPropagationPass();
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
  Optimization::CONST_MERGE
};

static void AddStandardCompilePasses(klee::LegacyLLVMPassManagerTy &PM) {
  PM.add(createVerifierPass());                  // Verify that input is correct

  // If the -strip-debug command line option was specified, do it.
  if (StripDebug)
    addPass(PM, createStripSymbolsPass(true));

  if (DisableOptimizations) return;

  std::vector<enum Optimization::StdOptimization> opts = (
      OptType.size() ? OptType: DefaultStdOptimizations); 

  for (unsigned i = 0; i < opts.size(); i++) {
    Pass *optPass = GenerateOptPass(opts[i]);
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
  AddStandardCompilePasses(Passes);

  if (!DisableOptimizations) {
    // Now that composite has been compiled, scan through the module, looking
    // for a main function.  If main is defined, mark all other functions
    // internal.
    if (!DisableInternalize) {
      ModulePass *pass = createInternalizePass(
          std::vector<const char *>(1, EntryPoint.c_str()));
      addPass(Passes, pass);
    }

    // Propagate constants at call sites into the functions they call.  This
    // opens opportunities for globalopt (and inlining) by substituting function
    // pointers passed as arguments to direct uses of functions.  
    addPass(Passes, createIPSCCPPass());

    // Now that we internalized some globals, see if we can hack on them!
    addPass(Passes, createGlobalOptimizerPass());

    // Linking modules together can lead to duplicated global constants, only
    // keep one copy of each constant...
    addPass(Passes, createConstantMergePass());

    // Remove unused arguments from functions...
    addPass(Passes, createDeadArgEliminationPass());

    // Reduce the code after globalopt and ipsccp.  Both can open up significant
    // simplification opportunities, and both can propagate functions through
    // function pointers.  When this happens, we often have to resolve varargs
    // calls, etc, so let instcombine do this.
    addPass(Passes, createInstructionCombiningPass());

    if (!DisableInline)
      addPass(Passes, createFunctionInliningPass()); // Inline small functions

    addPass(Passes, createPruneEHPass());            // Remove dead EH info
    addPass(Passes, createGlobalOptimizerPass());    // Optimize globals again.
    addPass(Passes, createGlobalDCEPass());          // Remove dead functions

    // If we didn't decide to inline a function, check to see if we can
    // transform it to pass arguments by value instead of by reference.
    addPass(Passes, createArgumentPromotionPass());

    // The IPO passes may leave cruft around.  Clean up after them.
    addPass(Passes, createInstructionCombiningPass());
    addPass(Passes, createJumpThreadingPass());        // Thread jumps.
    addPass(Passes, createScalarReplAggregatesPass()); // Break up allocas

    // Run a few AA driven optimizations here and now, to cleanup the code.
    addPass(Passes, createFunctionAttrsPass());      // Add nocapture
    addPass(Passes, createGlobalsModRefPass());      // IP alias analysis

    addPass(Passes, createLICMPass());               // Hoist loop invariants
    addPass(Passes, createGVNPass());                // Remove redundancies
    addPass(Passes, createMemCpyOptPass());          // Remove dead memcpy's
    addPass(Passes, createDeadStoreEliminationPass()); // Nuke dead stores

    // Cleanup and simplify the code after the scalar optimizations.
    addPass(Passes, createInstructionCombiningPass());

    addPass(Passes, createJumpThreadingPass());        // Thread jumps.
    addPass(Passes, createPromoteMemoryToRegisterPass()); // Cleanup jumpthread.
    
    // Delete basic blocks, which optimization passes may have killed...
    addPass(Passes, createCFGSimplificationPass());

    // Now that we have optimized the program, discard unreachable functions...
    addPass(Passes, createGlobalDCEPass());
  }

  // If the -s or -S command line options were specified, strip the symbols out
  // of the resulting program to make it smaller.  -s and -S are GNU ld options
  // that we are supporting; they alias -strip-all and -strip-debug.
  if (Strip || StripDebug)
    addPass(Passes, createStripSymbolsPass(StripDebug && !Strip));

#if 0
  // Create a new optimization pass for each one specified on the command line
  std::auto_ptr<TargetMachine> target;
  for (unsigned i = 0; i < OptimizationList.size(); ++i) {
    const PassInfo *Opt = OptimizationList[i];
    if (Opt->getNormalCtor())
      addPass(Passes, Opt->getNormalCtor()());
    else
      llvm::errs() << "llvm-ld: cannot create pass: " << Opt->getPassName()
                << "\n";
  }
#endif

  // The user's passes may leave cruft around. Clean up after them them but
  // only if we haven't got DisableOptimizations set
  if (!DisableOptimizations) {
    addPass(Passes, createInstructionCombiningPass());
    addPass(Passes, createCFGSimplificationPass());
    addPass(Passes, createAggressiveDCEPass());
    addPass(Passes, createGlobalDCEPass());
  }

  // Make sure everything is still good.
  if (!DontVerify)
    Passes.add(createVerifierPass());

  // Run our queue of passes all at once now, efficiently.
  Passes.run(*M);
}

}
