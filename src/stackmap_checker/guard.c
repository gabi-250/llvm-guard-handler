#include "stmap.h"
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

#define MAX_CALL_STACK_DEPTH 256
// XXX Need a portable way of obtaining the name of the executable
#define TRACE_BIN "trace"

// XXX These currently need to be global, since they need to be visible to
// jump.s
// The label to jump to whenever a guard fails.
extern void restore_and_jmp(void);
// The address to jump to.
uint64_t addr = 0;

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
                                         unw_context_t context)
{
    uint64_t *ret_addrs = calloc(MAX_CALL_STACK_DEPTH, sizeof(uint64_t));
    unw_word_t *bps = calloc(MAX_CALL_STACK_DEPTH, sizeof(unw_word_t));
    unw_word_t **registers = calloc(MAX_CALL_STACK_DEPTH, sizeof(unw_word_t *));
    uint32_t frame = 0;
    while (unw_step(&cursor) > 0) {
        unw_word_t off, pc;
        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (!pc) {
            break;
        }

        char fun_name[128];
        unw_get_proc_name(&cursor, fun_name, sizeof(fun_name), &off);

        // Stop when main is reached.
        if (!strcmp(fun_name, "main")) {
            break;
        }

        if (!registers[frame]) {
            // 16 registers are saved for each frame
            registers[frame] = calloc(16, sizeof(unw_word_t));
        }
        unw_get_reg(&cursor, UNW_X86_64_RAX, &registers[frame][0]);
        unw_get_reg(&cursor, UNW_X86_64_RDX, &registers[frame][1]);
        unw_get_reg(&cursor, UNW_X86_64_RCX, &registers[frame][2]);
        unw_get_reg(&cursor, UNW_X86_64_RBX, &registers[frame][3]);
        unw_get_reg(&cursor, UNW_X86_64_RSI, &registers[frame][4]);
        unw_get_reg(&cursor, UNW_X86_64_RDI, &registers[frame][5]);
        unw_get_reg(&cursor, UNW_X86_64_RBP, &registers[frame][6]);
        unw_get_reg(&cursor, UNW_X86_64_RSP, &registers[frame][7]);
        unw_get_reg(&cursor, UNW_X86_64_R8,  &registers[frame][8]);
        unw_get_reg(&cursor, UNW_X86_64_R9,  &registers[frame][9]);
        unw_get_reg(&cursor, UNW_X86_64_R10, &registers[frame][10]);
        unw_get_reg(&cursor, UNW_X86_64_R11, &registers[frame][11]);
        unw_get_reg(&cursor, UNW_X86_64_R12, &registers[frame][12]);
        unw_get_reg(&cursor, UNW_X86_64_R13, &registers[frame][13]);
        unw_get_reg(&cursor, UNW_X86_64_R14, &registers[frame][14]);
        unw_get_reg(&cursor, UNW_X86_64_R15, &registers[frame][15]);
        // Find address of the return address.
        ret_addrs[frame] =
            (uint64_t)(registers[frame][UNW_X86_64_RBP] + 8);
        // Store the current BP.
        *(bps + frame) = registers[frame][UNW_X86_64_RBP];
        frame++;
    }

    call_stack_state_t *state = malloc(sizeof(call_stack_state_t));
    state->ret_addrs = ret_addrs;
    state->bps = bps;
    state->registers = registers;
    state->depth = frame;
    state->records = NULL;
    return state;
}

void free_call_stack_state(call_stack_state_t *state)
{
    for(int frame = 0; frame < state->depth; ++frame) {
        free(state->registers[frame]);
    }
    free(state->ret_addrs);
    free(state->registers);
    free(state->bps);
    free(state->records);
    free(state);
}

/*
 * Return all the locations recorded in the stack map `sm` for each of
 * the frames in `state`. The direct locations need to be restored later.
 */
size_t get_locations(stack_map_t *sm, call_stack_state_t *state,
                     uint64_t **locs)
{
    size_t num_locations = 0;
    size_t new_size = 0;
    // Each record corresponds to a stack frame.
    for (int i = 0; i < state->depth; ++i) {
        stack_map_record_t opt_rec = state->records[i];
        stack_map_record_t *unopt_rec =
            stmap_get_map_record(sm, ~opt_rec.patchpoint_id);
        new_size += opt_rec.num_locations * sizeof(uint64_t);
        *locs = (uint64_t *)realloc(*locs, new_size);
        // Populate the stack of the optimized function with the values the
        // unoptimized function expects
        for (int j = 0; j < unopt_rec->num_locations - 1; j += 2) {
            location_type type = unopt_rec->locations[j].kind;
            uint64_t opt_location_value =
                stmap_get_location_value(sm, opt_rec.locations[j],
                                         state->registers[i],
                                         (void *)state->bps[i]);

            uint64_t loc_size =
                stmap_get_location_value(sm, opt_rec.locations[j + 1],
                                         state->registers[i],
                                         (void *)state->bps[i]);
            if (type == DIRECT) {
                memcpy(*locs + num_locations, &opt_location_value, loc_size);
                ++num_locations;
                memcpy(*locs + num_locations, &loc_size, sizeof(uint64_t));
                ++num_locations;
            } else if (type == REGISTER) {
                memcpy(*locs + num_locations, &opt_location_value, loc_size);
                ++num_locations;
                memcpy(*locs + num_locations, &loc_size, sizeof(uint64_t));
                ++num_locations;
            } else if (type == INDIRECT) {
                errx(1, "Not implemented - indirect.\n");
            } else if (type != CONSTANT && type != CONST_INDEX) {
                // A type which is not CONSTANT or CONST_INDEX is considered
                // unknown (no other types are defined in the stack map
                // documentation).
                errx(1, "Unknown record - %u. Exiting\n", type);
            }
        }
    }
    return num_locations;
}

void restore_unopt_stack(stack_map_t *sm, call_stack_state_t *state,
                         stack_map_record_t *unopt_rec,
                         stack_map_record_t *opt_rec)
{
    // This stores the stack map records of the unoptimized trace
    stack_map_record_t *unopt_call_stk_records = calloc(state->depth,
                                                  sizeof(stack_map_record_t));
    state->records = calloc(state->depth, sizeof(stack_map_record_t));
    // Overwrite the old return addresses and store the stack map records that
    // correspond to each call which generated this stack trace in
    // `unopt_call_stk_records`.
    unopt_call_stk_records[0] = *unopt_rec;
    // The first stack map record to be stored is the one associated with the
    // patchpoint which triggered the guard failure.
    state->records[0] = *opt_rec;
    // Loop `depth - 1` times, because the return address of 'trace' should not
    // be overwritten (it should still return in main)
    for (int i = 0; i < state->depth - 1; ++i) {
        // `sm_pos` identifies a position in a function. It is essentially an
        // address. A stack map record is always associated with a stack size
        // record. Each stack size record uniquely identifies a function, while
        // a stack map record contains the offset of the `stackmap` call in the
        // function.
        stack_map_pos_t *sm_pos =
            stmap_get_unopt_return_addr(sm, *(uint64_t *)state->ret_addrs[i]);
        uint64_t unopt_ret_addr =
            sm->stk_size_records[sm_pos->stk_size_record_index].fun_addr +
            sm->stk_map_records[sm_pos->stk_map_record_index].instr_offset;
        // Overwrite the old return addresses
        *(uint64_t *)state->ret_addrs[i] = unopt_ret_addr;
        // The stack map record associated with this frame.
        stack_map_record_t *opt_stk_map_rec =
            stmap_get_map_record(
                sm,
                ~sm->stk_map_records[sm_pos->stk_map_record_index].patchpoint_id);
        // The unoptimized counterpart of the record above.
        unopt_call_stk_records[i + 1] =
            sm->stk_map_records[sm_pos->stk_map_record_index];

        // Store each record that corresponds to a frame on the call stack.
        state->records[i + 1] = *opt_stk_map_rec;
        free(sm_pos);
    }

    uint64_t *locations = NULL;
    // Get all the locations that are 'live' in the 'optimized' version of the
    // call stack. These need to be restored, so that execution can resume in
    // the 'unoptimized' version. The 'unoptimized' version only contains calls
    // to `__unopt_` functions.
    size_t num_locations = get_locations(sm, state, &locations);

    // Used to index `locations`.
    int loc_index = 0;
    // Restore all the stacks on the call stack
    for (int frame = 0; frame < state->depth; ++frame) {
        // Get the unoptimized stack map record associated with this frame.
        stack_map_record_t unopt_rec = unopt_call_stk_records[frame];
        // Populate the stack of the optimized function with the values the
        // unoptimized function expects.
        // Records are considered in pairs (the counter is incremented by 2),
        // because each record at an odd index in the array represents the
        // size of the previous record.
        for (int j = 0; j < unopt_rec.num_locations - 1; j += 2) {
            location_type type = unopt_rec.locations[j].kind;
            uint64_t opt_location_value = locations[loc_index];
            uint64_t loc_size = locations[loc_index + 1];
            if (type == DIRECT) {
                uint64_t unopt_addr = (uint64_t)state->bps[frame] +
                    unopt_rec.locations[j].offset;
                // Place the live location at the correct stack location.
                memcpy((void *)unopt_addr, &opt_location_value,
                        loc_size);
                loc_index += 2;
            } else if (type == REGISTER) {
                uint64_t reg_num = unopt_rec.locations[j].dwarf_reg_num;
                // Save the new value of the register (it is restored later).
                memcpy(state->registers[frame] + reg_num,
                       &opt_location_value, loc_size);
                loc_index += 2;
            } else if (type == INDIRECT) {
                errx(1, "Not implemented - indirect.\n");
            } else if (type != CONSTANT && type != CONST_INDEX) {
                errx(1, "Unknown record - %u. Exiting\n", type);
            }
        }
    }
    free(locations);
    free(unopt_call_stk_records);
}

/*
 * The guard failure handler. This is the callback passed to the `patchpoint`
 * call which represents a guard failure. When the `patchpoint` instruction is
 * executed, the callback is called.
 */
void __guard_failure(int64_t sm_id)
{
    fprintf(stderr, "Guard %ld failed!\n", sm_id);

    unw_cursor_t cursor;
    unw_context_t context;
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);
    unw_cursor_t saved_cursor = cursor;
    // Get the call stack state.
    call_stack_state_t *state = get_call_stack_state(cursor, context);

    // Read the stack map section.
    void *stack_map_addr = get_addr(".llvm_stackmaps");
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

    // Get the end address of the function in which a guard failed.
    void *end_addr = get_sym_end((void *)opt_size_rec->fun_addr, TRACE_BIN);
    uint64_t callback_ret_addr = (uint64_t) __builtin_return_address(0);
    // Check if the guard failed in an inlined function or not.
    if (callback_ret_addr >= opt_size_rec->fun_addr &&
        callback_ret_addr < (uint64_t)end_addr) {
        fprintf(stderr, "A guard failed, but not in an inlined func\n");
    } else {
        fprintf(stderr, "A guard failed in an inlined function.\n");
        // XXX WIP
        stack_map_record_t *rec = stmap_first_rec_after_addr(sm,
                callback_ret_addr);
        stack_size_record_t *size_rec =
            stmap_get_size_record(sm, rec->index);
        // Find the 'return addresses' of the inlined functions
        while(rec->instr_offset + size_rec->fun_addr != state->ret_addrs[0]) {
            stack_map_record_t *unopt_rec =
                stmap_get_map_record(sm, ~rec->patchpoint_id);
            stack_size_record_t *unopt_size_rec =
                stmap_get_size_record(sm, unopt_rec->index);
            rec =
                stmap_first_rec_after_addr(sm,
                                rec->instr_offset + size_rec->fun_addr + 1);
            if (!rec) {
                break;
            }
            size_rec = stmap_get_size_record(sm, rec->index);
            if (!size_rec) {
                errx(1, "Size record not found\n");
            }
        }
        exit(1);
    }

    // Restore the stack state.
    restore_unopt_stack(sm, state, unopt_rec, opt_rec);
    // Restore the register state.
    uint32_t frame = 0;
    while (unw_step(&saved_cursor) > 0) {
        unw_word_t off, pc;
        if (!pc) {
            break;
        }
        for (int i = 0; i < 16; ++i) {
            if (i != UNW_X86_64_RSP && i != UNW_X86_64_RBP) {
                unw_set_reg(&cursor, i, state->registers[frame][i]);
            }
        }
        ++frame;
    }
    // The address to jump to
    addr = unopt_size_rec->fun_addr + unopt_rec->instr_offset;
    stmap_free(sm);
    free_call_stack_state(state);
    asm volatile("jmp restore_and_jmp");
}

