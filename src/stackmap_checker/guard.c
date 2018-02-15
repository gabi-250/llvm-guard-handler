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
    int inlined = 0;
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

    call_stack_state_t *restored_state = malloc(sizeof(call_stack_state_t));
    restored_segment_t seg;
    // Check if the guard failed in an inlined function or not.
    if (callback_ret_addr >= opt_size_rec->fun_addr &&
        callback_ret_addr < (uint64_t)end_addr) {
        fprintf(stderr, "A guard failed, but not in an inlined func\n");
    } else {
        fprintf(stderr, "A guard failed in an inlined function.\n");
        inlined = 1;

        stack_map_record_t *rec = NULL;
            //stmap_first_rec_after_addr(sm,
            //    callback_ret_addr);
        stack_size_record_t *size_rec =
            stmap_get_size_record(sm, rec->index);
        uint64_t size_to_alloca = 0;


        // The stack map records which correspond to each call on the stack.
        stack_map_record_t *new_records = NULL;
        // The number of frames on the stack.
        uint32_t new_depth = 0;

        uint32_t *sizes = NULL;


        sizes = malloc(sizeof(uint32_t));
        stack_size_record_t *unopt_size_rec =
            stmap_get_size_record(sm, unopt_rec->index);

        sizes[0] = unopt_size_rec->stack_size;
        uint64_t *saved_ret_addrs = calloc(MAX_CALL_STACK_DEPTH,
                                           sizeof(uint64_t));
        // Find the 'return addresses' of the inlined functions
        while(rec->instr_offset + size_rec->fun_addr != state->ret_addrs[0]) {
            stack_map_record_t *unopt_rec =
                stmap_get_map_record(sm, ~rec->patchpoint_id);
            stack_size_record_t *unopt_size_rec =
                stmap_get_size_record(sm, unopt_rec->index);

            ++new_depth;
            sizes = realloc(sizes, (new_depth + 1) * sizeof(uint32_t));

            new_records = realloc(new_records,
                                  new_depth * sizeof(stack_map_record_t));
            new_records[new_depth - 1] = *rec;
            sizes[new_depth] = unopt_size_rec->stack_size;
            saved_ret_addrs[new_depth - 1] =
                unopt_rec->instr_offset + unopt_size_rec->fun_addr;
            uint64_t opt_ret_addr = rec->instr_offset + size_rec->fun_addr + 1;
            rec = NULL;
                //stmap_first_rec_after_addr(sm, opt_ret_addr);
            if (!rec) {
                break;
            }
            size_rec = stmap_get_size_record(sm, rec->index);
            if (!size_rec) {
                errx(1, "Size record not found\n");
            }
        }

        for (size_t i = 0; i < new_depth; ++i) {
            // XXX calling convention, function arguments?... 8 for the ret addr
            size_to_alloca += sizes[i] + 8; // + ((i + 1) < new_depth? 8 : 0);
        }

        // The alloca has to happen here
        seg.start_addr            = (uint64_t)alloca(size_to_alloca);
        seg.total_size            = size_to_alloca;
        seg.size                  = unopt_size_rec->stack_size;
        restored_state->depth     = new_depth;
        restored_state->records   = new_records;
        get_restored_state(sm, state, seg, saved_ret_addrs, sizes);
    }

    if (inlined) {
    //    combine_states(restored_state, state, *opt_rec);
    //
        state = restored_state;

    } else {
        append_record(state, *opt_rec);
        restore_register_state(state, r);
    }

    // Restore the stack state.
    restore_unopt_stack(sm, state);
    restore_register_state(state, r);
    // The address to jump to
    addr = unopt_size_rec->fun_addr + unopt_rec->instr_offset;
    stmap_free(sm);
    //free_call_stack_state(state);

    if (inlined) {
        // must move the new 'frames'
        restored_start_addr =
            (uint64_t)((char *)seg.start_addr + seg.total_size - 9);
        restored_stack_size = 0; // seg.size - 8;
        asm volatile("jmp restore_inlined");
    } else {
        asm volatile("jmp jmp_to_addr");
    }
}

