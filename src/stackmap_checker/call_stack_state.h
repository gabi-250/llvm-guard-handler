#ifndef CALL_STACK_STATE_H
#define CALL_STACK_STATE_H

#include "stmap.h"
#include <stdbool.h>

#define MAX_CALL_STACK_DEPTH 256
#define REGISTER_COUNT 16
#define ADDR_SIZE sizeof(void *)


/*
 * A stack frame.
 *
 * The `real_bp` is the base pointer relative to which the live locations are
 * identified. If a function is inlined, an artificial frame is created for it.
 * The artificial frame will have its own base pointer, different from the
 * 'real' base pointer. The `real_bp` is the BP of the function in which the
 * inlined function was inlined. It is important to keep track of the 'real_bp'
 * of the frame, and of the artificially created one. The artifical base
 * pointer is used to place values at the correct offset in the newly created
 * frame.
 */
typedef struct Frame {
    // The address of the return address of the frame.
    uint64_t ret_addr;
    // The original return address of the frame.
    uint64_t stored_ret_addr;
    // The stack size (in the prologue/epilogue).
    uint64_t size;
    // The base pointer.
    unw_word_t bp;
    // The 'real' base pointer. bp != real_bp for inlined functions.
    unw_word_t real_bp;
    // The 16 registers recorded for each frame.
    unw_word_t *registers;
    // The stack map record which correspond to this call.
    stack_map_record_t record;
    // The stack map record which correspond to this call.
    stack_map_record_t real_record;
    // Whether this is the frame of an inlined function.
    bool inlined;
} frame_t;

// The state of the call stack.
typedef struct CallStackState {
    frame_t *frames;
    uint32_t depth;
} call_stack_state_t;

// The memory area which represents the restored call stack.
typedef struct RestoredStackSegment {
    uint64_t start_addr;
    uint64_t total_size;
} restored_segment_t;

/*
 * Return the state of the call stack.
 */
call_stack_state_t* get_call_stack_state(unw_cursor_t cursor);

/*
 * Free `state` and all the frames it stores.
 */
void free_call_stack_state(call_stack_state_t *state);

/*
 * Return the call_stack_state_t associated with the records which correspond
 * to inlined functions.
 *
 * Start collecting records from `start_addr`, and stop when a record whose
 * patchpoint_id is equal to `ppid` is found.
 */
call_stack_state_t* get_restored_state(stack_map_t *sm, uint64_t start_addr,
                                       uint64_t ppid);

/*
 * Populates the 'call stack' described by `seg` using the infromation in
 * `state`.
 *
 * This treats the addresses in the range
 * [seg.start_addr, seg.start_addr + total_size] as a call stack, and places
 * the return addresses and base pointers of `state` at the correct offset.
 */
void insert_real_addresses(call_stack_state_t *state, restored_segment_t seg);

/*
 * Return all the locations recorded in the stack map `sm` for each of
 * the frames in `state`. The direct locations need to be restored later.
 */
size_t get_locations(stack_map_t *sm, call_stack_state_t *state,
                     uint64_t **locs);

/*
 * Restore the values in each of the stack frames stored in `state`.
 */
void restore_unopt_stack(stack_map_t *sm, call_stack_state_t *state);

/*
 * Attempts to restore the register state of the last frame, using the
 * information in `state`.
 */
void restore_register_state(call_stack_state_t *state, uint64_t r[]);

/*
 * Collects the stackmap record associated with each frame in `state`.
 *
 * Each frame has a record associated with it, which is generated by the
 * stackmap call that marks the callsite which produced the frame.
 */
void collect_map_records(call_stack_state_t *state, stack_map_t *sm);

/*
 * Insert the specified frames at position `index` in `state`.
 */
void insert_frames(call_stack_state_t *state, size_t index,
                   frame_t *frames, size_t num_frames);

/*
 * Allocate and return `num_frames` empty frames.
 */
frame_t* alloc_empty_frames(size_t num_frames);

/*
 * Return a shallow copy of `state`.
 */
call_stack_state_t* get_state_copy(call_stack_state_t *state);

/*
 * Free `num_frames` frames from address `frames`.
 */
void free_frames(frame_t *frames, size_t num_frames);

/*
 * Insert into `state` all the frames of the found inlined functions.
 *
 * This only searches for inconsistencies between consecutive frames. The
 * return address of a frame at index i must always be an address inside the
 * function at index i + 1.
 */
bool collect_inlined_frames(call_stack_state_t *state, stack_map_t *sm);

/*
 * Return the patchpoint ID of the first record after `addr` in the function
 * indicated by `size_rec`.
 */
uint64_t get_next_patchpoint(stack_map_t *sm, uint64_t addr,
                             stack_size_record_t *size_rec);

/*
 * Return the total size of the call stack, including the space occupied by
 * return addresses.
 */
uint64_t get_total_stack_size(call_stack_state_t *state);

#endif // CALL_STACK_STATE_H
