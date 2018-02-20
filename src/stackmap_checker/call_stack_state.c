#include "call_stack_state.h"
#include "stmap.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#define MAX_BUF_SIZE 128

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
        char fun_name[MAX_BUF_SIZE];
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
        // Store the address of the return address.
        ret_addrs[frame] =
            (uint64_t)(registers[frame][UNW_X86_64_RBP] + 8);
        // Store the current BP.
        *(bps + frame) = registers[frame][UNW_X86_64_RBP];
        frame++;
    }
    call_stack_state_t *state = malloc(sizeof(call_stack_state_t));
    state->ret_addrs = ret_addrs;
    state->bps       = bps;
    state->registers = registers;
    state->depth     = frame;
    state->records   = NULL;
    return state;
}

void collect_map_records(call_stack_state_t *state, stack_map_t *sm)
{
    state->records = calloc(state->depth - 1, sizeof(stack_map_record_t));
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
            sm->stk_map_records[sm_pos->stk_map_record_index].instr_offset +
            PATCHPOINT_CALL_SIZE;
        // Overwrite the old return addresses
        *(uint64_t *)state->ret_addrs[i] = unopt_ret_addr;
        // The stack map record associated with this frame.
        stack_map_record_t *opt_stk_map_rec =
            stmap_get_map_record(
                sm,
                ~sm->stk_map_records[sm_pos->stk_map_record_index].patchpoint_id);
        // Store each record that corresponds to a frame on the call stack.
        state->records[i] = *opt_stk_map_rec;
        free(sm_pos);
    }
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

size_t get_locations(stack_map_t *sm, call_stack_state_t *state,
                     uint64_t **locs)
{
    size_t num_locations = 0;
    size_t new_size = 0;
    // Each record corresponds to a stack frame.
    for (size_t i = 0; i < state->depth; ++i) {
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

void restore_unopt_stack(stack_map_t *sm, call_stack_state_t *state)
{
    uint64_t *locations = NULL;
    // Get all the locations that are 'live' in the 'optimized' version of the
    // call stack. These need to be restored, so that execution can resume in
    // the 'unoptimized' version. The 'unoptimized' version only contains calls
    // to `__unopt_` functions.
    size_t num_locations = get_locations(sm, state, &locations);
    // This is used to index `locations`.
    int loc_index = 0;
    // Restore all the stacks on the call stack
    for (int frame = 0; frame < state->depth; ++frame) {
        // Get the unoptimized stack map record associated with this frame.
        stack_map_record_t *unopt_rec = stmap_get_map_record(sm,
                ~state->records[frame].patchpoint_id);
        // Populate the stack of the optimized function with the values the
        // unoptimized function expects.
        // Records are considered in pairs (the counter is incremented by 2),
        // because each record at an odd index in the array represents the
        // size of the previous record.
        for (int j = 0; j < unopt_rec->num_locations - 1; j += 2) {
            location_type type = unopt_rec->locations[j].kind;
            uint64_t opt_location_value = locations[loc_index];
            uint64_t loc_size = locations[loc_index + 1];
            if (type == DIRECT) {
                uint64_t unopt_addr = (uint64_t)state->bps[frame] +
                    unopt_rec->locations[j].offset;
                memcpy((void *)unopt_addr, &opt_location_value,
                        loc_size);
                loc_index += 2;
            } else if (type == REGISTER) {
                uint16_t reg_num = unopt_rec->locations[j].dwarf_reg_num;
                assert_valid_reg_num(reg_num);
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
}

void append_record(call_stack_state_t *state, stack_map_record_t first_rec)
{
    // The first stack map record to be stored is the one associated with the
    // patchpoint which triggered the guard failure (so it needs to be added
    // separately).
    state->records = realloc(state->records,
                             state->depth * sizeof(stack_map_record_t));
    memmove(state->records + 1, state->records,
            (state->depth - 1) * sizeof(stack_map_record_t));
    state->records[0] = first_rec;
}

void restore_register_state(call_stack_state_t *state, uint64_t r[])
{
    for (size_t i = 0; i < 16; ++i) {
        r[i] = (uint64_t)state->registers[0][i];
    }
}

