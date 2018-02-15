#ifndef CALL_STACK_STATE_H
#define CALL_STACK_STATE_H

#include "stmap.h"
#define UNW_LOCAL_ONLY
#include <libunwind.h>

#define MAX_CALL_STACK_DEPTH 256

// The state of the call stack.
typedef struct CallStackState {
    // The address of the return address of each frame.
    uint64_t *ret_addrs;
    // The base pointers.
    unw_word_t *bps;
    // The 16 registers recorded for each frame.
    unw_word_t **registers;
    // The stack map records which correspond to each call on the stack.
    stack_map_record_t *records;
    // The number of frames on the stack.
    uint32_t depth;
} call_stack_state_t;

/*
 * Return the state of the call stack.
 */
call_stack_state_t* get_call_stack_state(unw_cursor_t cursor,
                                         unw_context_t context);

void free_call_stack_state(call_stack_state_t *state);

/*
 * Return all the locations recorded in the stack map `sm` for each of
 * the frames in `state`. The direct locations need to be restored later.
 */
size_t get_locations(stack_map_t *sm, call_stack_state_t *state,
                     uint64_t **locs);

void restore_unopt_stack(stack_map_t *sm, call_stack_state_t *state);

void restore_register_state(unw_cursor_t cursor, call_stack_state_t *state);

void collect_map_records(call_stack_state_t *state, stack_map_t *sm);

void append_record(call_stack_state_t *dest, stack_map_record_t first_rec);
#endif // CALL_STACK_STATE_H
