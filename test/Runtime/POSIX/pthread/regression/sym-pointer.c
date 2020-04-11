// RUN: %clang %s -emit-llvm %O0opt -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error --cutoff-events=0 %t.bc 2>&1 | FileCheck %s

#include "klee/klee.h"

int a = 4;
int b = 2;
volatile int *p;

static klee_sync_primitive_t lock;

static void emit_events(void) {
	klee_lock_acquire(&lock);
	klee_lock_release(&lock);
}

static void thread(void *arg) {
	emit_events();

	int x = *p;

	// CHECK-DAG: x: 4
	// CHECK-DAG: x: 4
	// CHECK-DAG: x: 2
	// CHECK-DAG: x: 2
	printf("x: %d\n", x);
}

int main(void) {
	printf("&a: %p\n", &a);
	printf("&b: %p\n", &b);

	klee_make_symbolic(&p, sizeof(p), "p");
	klee_assume(p == &a | p == &b);

	klee_create_thread(thread, NULL);

	emit_events();

	int x = *p;

	// CHECK-DAG: x: 4
	// CHECK-DAG: x: 4
	// CHECK-DAG: x: 2
	// CHECK-DAG: x: 2
	printf("x: %d\n", x);

	return 0;

	// CHECK-DAG: KLEE: done: completed paths = 4
}
