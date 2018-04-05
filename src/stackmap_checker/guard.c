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


// These need to be global, since they need to be visible to jump.s
uint64_t addr = 0;
uint64_t r[REGISTER_COUNT];
uint64_t restored_bp = 0;
uint64_t restored_stack_size = 0;

// The stack of jump_inlined will be overwritten, but
// restored_start_addr must be freed.
uint64_t restored_start_addr = 0;
// restored_total_size is needed to memset the old stack to 0,
// after the stack of jump_inlined is overwritten.
uint64_t restored_total_size = 0;

/*
 * If necessary, increase the size of the stack according to `seg`, and then
 * jump to the `restore_inlined` label.
 */
void jump_inlined(call_stack_state_t *state, restored_segment_t seg)
{
    // rbp and rsp must be set correctly, so the new 'frames' appear to have
    // always existed on the call stack.
    uint64_t real_rsp;
    asm volatile("mov %%rsp, %0" : "=r"(real_rsp) : : );
    uint64_t main_bp =
        state->frames[state->depth - 1].registers[UNW_X86_64_RBP];
    if (main_bp - seg.total_size < real_rsp) {
        // Grow the stack.
        jump_inlined(state, seg);
    } else {
        uint64_t first_size = state->frames[0].size - ADDR_SIZE;
        restored_stack_size = first_size;
        restored_bp = main_bp + first_size;
        uint64_t cur_bp = restored_bp;
        for (size_t i = 1; i + 1 < state->depth; ++i) {
            cur_bp += state->frames[i].size + ADDR_SIZE;
            *(uint64_t *)state->frames[i - 1].bp = cur_bp;
        }
        free_call_stack_state(state);
        restored_start_addr = seg.start_addr;
        restored_total_size = seg.total_size;
        memcpy((void *)main_bp, (void *)seg.start_addr, seg.total_size);
        memset((void *)restored_start_addr, 0, restored_total_size);
        free((void *)restored_start_addr);
        asm volatile("jmp restore_inlined");
    }
}

/*
 * The guard failure handler. This is the callback passed to the `patchpoint`
 * call which represents a guard failure. When the `patchpoint` instruction is
 * executed, the callback is called.
 */
void __guard_failure(int64_t sm_id)
{
    fprintf(stderr, "Guard %ld failed!\n", sm_id);
    char *binary_path = get_binary_path();
    unw_cursor_t cursor;
    unw_context_t context;
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);
    unw_cursor_t saved_cursor = cursor;
    // Read the stack map section.
    void *stack_map_addr = get_addr(binary_path, ".llvm_stackmaps");
    free(binary_path);
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
    call_stack_state_t *state = get_call_stack_state(cursor);
    collect_map_records(state, sm);
    // Are there any inlined functions?
    bool inlined  = collect_inlined_frames(state, sm);
    // Get the end address of the function in which a guard failed.
    void *end_addr = (void *)get_sym_end(opt_size_rec->fun_addr);
    uint64_t callback_ret_addr = (uint64_t) __builtin_return_address(0);
    // If any inlining happened, it is necessary to reconstruct the entire
    // stack. If that is the case, `seg` will contain all the information
    // necessary to point the rsp and the rbp to the correct addresses.
    restored_segment_t seg;
    // Create the first frame (this corresponds to the record associated with
    // the guard that failed).
    frame_t *fail_frame = alloc_empty_frames(1);
    fail_frame->record = fail_frame->real_record = *opt_rec;
    fail_frame->size = unopt_size_rec->stack_size;
    fail_frame->real_bp = fail_frame->bp = state->frames[0].real_bp;
    memcpy(fail_frame->registers, state->frames[0].registers,
           REGISTER_COUNT * sizeof(unw_word_t));
    uint64_t first_ret_addr = opt_rec->instr_offset + opt_size_rec->fun_addr;
    fail_frame->ret_addr = first_ret_addr;
    // Check if the guard failed in an inlined function or not.
    if (callback_ret_addr >= opt_size_rec->fun_addr &&
        callback_ret_addr < (uint64_t)end_addr) {
        fprintf(stderr, "A guard failed, but not in an inlined func\n");
        // The first stack map record to be stored is the one associated with
        // the patchpoint which triggered the guard failure (so it needs to be
        // added separately).
        insert_frames(state, 0, fail_frame, 1);
    } else {
        fprintf(stderr, "A guard failed in an inlined function.\n");
        inlined = 1;
        uint64_t last_ppid = state->frames[0].record.patchpoint_id;
        // The record which corresponds to the guard that failed returns
        // in `callback_ret_addr`, which is not an address of the function
        // that corresponds to the previous stack frame. Since the stack
        // map call that generated the guard failure was inlined, the
        // stack frames must be reconstructed, starting in the function
        // in which the inlining happened.
        stack_size_record_t *real_size_rec =
            stmap_get_size_record_in_func(sm,
                                          get_sym_start(callback_ret_addr));
        uint64_t next_ppid = get_next_patchpoint(sm, callback_ret_addr,
                                                 real_size_rec);
        // The function in which the guard fail might have been inlined in
        // a function that was inlined. The 'inlining nesting' can be
        // arbitrarily deep, so find all the frames which do no not 'belong'
        // to the current function (according to the stack map record ID
        // ordering). Find all such frames in between the guard and the
        // stack map with ID = `next_ppid`.
        call_stack_state_t *restored_state =
            get_restored_state(sm, callback_ret_addr, next_ppid);
        for (size_t i = 0; i < restored_state->depth; ++i) {
            // The `real_bp` is the base pointer relative to which the live
            // locations are identified. If a function is inlined, an
            // artificial frame is created for it. It is important to keep
            // track of the 'real_bp' of the frame, and of the artificially
            // created one. The artifical base pointer is used to place values
            // at the correct offset in the newly created frame.
            restored_state->frames[i].real_bp = fail_frame->bp;
            restored_state->frames[i].registers = calloc(REGISTER_COUNT,
                                                         sizeof(unw_word_t));
            memcpy(restored_state->frames[i].registers, fail_frame->registers,
                    REGISTER_COUNT * sizeof(unw_word_t));
        }
        insert_frames(state, 0, restored_state->frames, restored_state->depth);
        free_call_stack_state(restored_state);
        // A guard failed in an inlined function, so fail_frame corresponds to a
        // function call that never happened.
        fail_frame->inlined = 1;
        // Insert the frame of the function in which the guard failed.
        insert_frames(state, 0, fail_frame, 1);
    }
    free_frames(fail_frame, 1);
    if (inlined) {
        // If any inlining happened, a new call stack must be created.
        get_total_stack_size(state);
        seg.total_size            = get_total_stack_size(state);
        seg.start_addr            = (uint64_t)malloc(seg.total_size);
        insert_real_addresses(state, seg);
    }
    // Restore the stack and register state.
    restore_unopt_stack(sm, state);
    restore_register_state(state, r);
    // The address to jump to
    addr = unopt_size_rec->fun_addr + unopt_rec->instr_offset;
    stmap_free(sm);
    if (inlined) {
        jump_inlined(state, seg);
    } else {
        free_call_stack_state(state);
        asm volatile("jmp jmp_to_addr");
    }
}
