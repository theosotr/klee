; REQUIRES: llvm-3.4
; RUN: llvm-as %s -f -o %t.bc
; RUN: rm -rf %t.klee-out
; RUN: not %klee --output-dir=%t.klee-out %t.bc 2> %t.log
; RUN: FileCheck --input-file %t.log %s

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@a = internal global i32 0, align 4
@b = internal global [1 x i32] [i32 1], align 4

define i32 @main() {
; CHECK: Overshift during constant folding
  ret i32 ashr (i32 zext (i1 icmp ne (i32* getelementptr inbounds ([1 x i32]* @b, i64 0, i64 0), i32* @a) to i32), i32 trunc (i64 add (i64 zext (i1 icmp ne (i32* getelementptr inbounds ([1 x i32]* @b, i64 0, i64 0), i32* @a) to i64), i64 31) to i32))
}
