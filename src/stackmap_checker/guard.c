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
uint64_t r[16];
uint64_t restored_start_addr = 0;
uint64_t restored_stack_size = 0;

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

    bool inlined = 0;
    // Get the call stack state.
    call_stack_state_t *state = get_call_stack_state(cursor);
    call_stack_state_t *state_copy = get_state_copy(state);
    collect_map_records(state, sm);
    uint32_t old_depth = state->depth;
    collect_inlined_frames(state, sm);
    inlined = old_depth != state->depth;
    // Get the end address of the function in which a guard failed.
    void *end_addr = get_sym_end(binary_path, (void *)opt_size_rec->fun_addr);
    uint64_t callback_ret_addr = (uint64_t) __builtin_return_address(0);
    restored_segment_t seg;
    // Check if the guard failed in an inlined function or not.
    if (callback_ret_addr >= opt_size_rec->fun_addr &&
        callback_ret_addr < (uint64_t)end_addr) {
        fprintf(stderr, "A guard failed, but not in an inlined func\n");
        // The first stack map record to be stored is the one associated with the
        // patchpoint which triggered the guard failure (so it needs to be added
        // separately).
        state->frames[0].record = *opt_rec;
        restore_register_state(state, r);
    } else {
        fprintf(stderr, "A guard failed in an inlined function.\n");
        inlined = 1;
        uint64_t last_addr =
            state->depth ? state->frames[state->depth - 1].ret_addr :
                            state->main_ret_addr;
        call_stack_state_t *restored_state =
            get_restored_state(sm, callback_ret_addr, last_addr);


        uint64_t size_to_alloca = 0;
        for (size_t i = 0; i < state->depth; ++i) {
            // XXX calling convention, function arguments?... 8 for the ret addr
            size_to_alloca += restored_state->frames[i].size + 8;
        }
        uint64_t last_bp = state->main_bp;
        uint64_t last_ret_addr = state->main_ret_addr;
        // The alloca has to happen here
        // make sure frame[0] contains the register state of the function in
        // which the other functions were inlined (main for example)
    }
    // Restore the stack state.
    restore_unopt_stack(sm, state);
    restore_register_state(state, r);
    // The address to jump to
    addr = unopt_size_rec->fun_addr + unopt_rec->instr_offset;
    stmap_free(sm);
    free_call_stack_state(state);
    if (inlined) {
        // must move the new 'frames'
        restored_start_addr =
            (uint64_t)((char *)seg.start_addr); //+ seg.total_size - 16);
        restored_stack_size = seg.size - 8;
        asm volatile("jmp restore_inlined");
    } else {
        asm volatile("jmp jmp_to_addr");
    }
}

