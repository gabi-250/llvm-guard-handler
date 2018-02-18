#include "stmap.h"
#include "call_stack_state.h"
#include "utils.h"
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <signal.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>


// XXX These currently need to be global, since they need to be visible to
// jump.s
uint64_t addr = 0;

/*
 * The guard failure handler. This is the callback passed to the `patchpoint`
 * call which represents a guard failure. When the `patchpoint` instruction is
 * executed, the callback is called.
 */
void __guard_failure(int64_t sm_id)
{
    fprintf(stderr, "Guard %ld failed!\n", sm_id);
    // XXX Need a portable way of obtaining the name of the executable
    char *binary_path = get_binary_path();
    unw_cursor_t cursor;
    unw_context_t context;
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);
    unw_cursor_t saved_cursor = cursor;
    // Read the stack map section.
    void *stack_map_addr = get_addr(binary_path, ".llvm_stackmaps");
    if (!stack_map_addr) {
        errx(1, ".llvm_stackmaps section not found. Exiting.\n");
    }
    stack_map_t *sm = stmap_create(stack_map_addr);

    // The stack map records which correspond to the optimized/unoptimized
    // versions of the function in which the guard failed.
    stack_map_record_t *opt_rec = stmap_get_map_record(sm, sm_id);
    stack_map_record_t *unopt_rec = stmap_get_map_record(sm, ~sm_id);
    if (!unopt_rec || !opt_rec) {
        errx(1, "Stack map record not found. Exiting.\n");
    }

    // The stack size records associated with the map records above.
    stack_size_record_t *unopt_size_rec =
        stmap_get_size_record(sm, unopt_rec->index);
    stack_size_record_t *opt_size_rec =
        stmap_get_size_record(sm, opt_rec->index);
    if (!unopt_size_rec || !opt_size_rec) {
        errx(1, "Record not found.");
    }

    // Get the call stack state.
    call_stack_state_t *state = get_call_stack_state(cursor, context);
    collect_map_records(state, sm);
    // Get the end address of the function in which a guard failed.
    void *end_addr = get_sym_end(binary_path, (void *)opt_size_rec->fun_addr);
    uint64_t callback_ret_addr = (uint64_t) __builtin_return_address(0);

    // Check if the guard failed in an inlined function or not.
    if (callback_ret_addr >= opt_size_rec->fun_addr &&
        callback_ret_addr < (uint64_t)end_addr) {
        fprintf(stderr, "A guard failed, but not in an inlined func\n");
    } else {
        fprintf(stderr, "A guard failed in an inlined function.\n");
        exit(1);
    }
    append_record(state, *opt_rec);

    // Restore the stack state.
    restore_unopt_stack(sm, state);
    restore_register_state(saved_cursor, state);
    // The address to jump to
    addr = unopt_size_rec->fun_addr + unopt_rec->instr_offset;
    stmap_free(sm);
    free_call_stack_state(state);
    free(binary_path);
    asm volatile("jmp jmp_to_addr");
}

