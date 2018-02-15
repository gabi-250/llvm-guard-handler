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

void get_restored_state(stack_map_t *sm,
        call_stack_state_t *state,
        restored_segment_t seg, uint64_t *saved_ret_addrs,
        uint32_t *sizes)
{
    uint64_t *new_ret_addrs = calloc(state->depth, sizeof(uint64_t));
    unw_word_t *new_bps = calloc(state->depth, sizeof(unw_word_t));
    unw_word_t **new_registers = calloc(state->depth, sizeof(unw_word_t *));
    // now add the return addresses on the new 'stack' and save the bps
    // start with the high address
    char *cur_bp = (char *) seg.start_addr + seg.total_size;
    for (size_t i = 0; i < state->depth; ++i) {

        cur_bp -= 8;
        // returns in __unopt_more_indirection, where the PP failed
        // must add that as the first stack size
        new_ret_addrs[i] = (uint64_t)(cur_bp + 8);
        *(uint64_t *)new_ret_addrs[i] = saved_ret_addrs[i];
        new_registers[i] = calloc(16, sizeof(unw_word_t));

        new_bps[i] = (uint64_t)cur_bp;

        if (i > 0) {
            *(uint64_t *)new_bps[i - 1] = new_bps[i];
        }
        cur_bp -= sizes[i];
    }
    *(uint64_t *)new_bps[state->depth - 1] = state->bps[0];

    state->ret_addrs = new_ret_addrs;
    state->bps       = new_bps;
    state->registers = new_registers;
}

size_t get_locations(stack_map_t *sm, call_stack_state_t *state,
                     uint64_t **locs)
{
    size_t num_locations = 0;
    size_t loc_index = 0;
    size_t new_size = 0;
    // Each record corresponds to a stack frame.
    for (size_t i = 0; i < state->depth; ++i) {
        stack_map_record_t opt_rec = state->records[i];
        stack_map_record_t *unopt_rec =
            stmap_get_map_record(sm, ~opt_rec.patchpoint_id);
        num_locations += opt_rec.num_locations;
        new_size = num_locations * sizeof(uint64_t);
        if (!new_size) {
            continue;
        }
        *locs = (uint64_t *)realloc(*locs, new_size);

        // Populate the stack of the optimized function with the values the
        // unoptimized function expects
        for (size_t j = 0; j + 1 < unopt_rec->num_locations; j += 2) {
            location_type type = unopt_rec->locations[j].kind;
            void *opt_location_value = NULL;
            uint64_t *loc_size = NULL;
            // First, retrieve the size of the location at index `j`. This will
            // be stored in `loc_size`. The size of the location which
            // represents the size of location `j` is a `uint64_t` value,
            // because `LiveVariablesPass` records location sizes as 64-bit
            // values.
            stmap_get_location_value(sm, opt_rec.locations[j + 1],
                                     state->registers[i],
                                     (void *)state->bps[i],
                                     (void *)&loc_size, sizeof(uint64_t));
            // Now, copy `loc_size` bytes starting at the address indicated by
            // the location at position `j`.
            stmap_get_location_value(sm, opt_rec.locations[j],
                                     state->registers[i],
                                     (void *)state->bps[i],
                                     &opt_location_value,
                                     *loc_size);
            uint64_t location_value_addr = (uint64_t) opt_location_value;
            uint64_t loc_size_addr = (uint64_t) loc_size;
            if (type == DIRECT || type == REGISTER) {
                memcpy(*locs + loc_index, &location_value_addr, sizeof(uint64_t));
                ++loc_index;
                memcpy(*locs + loc_index, &loc_size_addr, sizeof(uint64_t));
                ++loc_index;
            } else if (type == INDIRECT) {
                errx(1, "Not implemented - indirect.\n");
            } else if (type != CONSTANT && type != CONST_INDEX) {
                // A type which is not CONSTANT or CONST_INDEX is considered
                // unknown (no other types are defined in the stack map
                // documentation).
                errx(1, "Unknown record - %u. Exiting\n", type);
            } else {
                free(opt_location_value);
                free(loc_size);
            }
        }
    }
    return num_locations;
}

void free_locations(uint64_t *locations, size_t num_locations)
{
    for (size_t i = 0; i < num_locations; ++i) {
        free((void *)locations[i]);
    }
    free(locations);
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
            uint64_t opt_location_addr = locations[loc_index];
            uint64_t loc_size = *(uint64_t *)locations[loc_index + 1];
            if (type == DIRECT) {
                uint64_t unopt_addr = (uint64_t)state->bps[frame] +
                    unopt_rec->locations[j].offset;
                memcpy((void *)unopt_addr, (void *)opt_location_addr,
                        loc_size);
                loc_index += 2;
            } else if (type == REGISTER) {
                uint16_t reg_num = unopt_rec->locations[j].dwarf_reg_num;
                assert_valid_reg_num(reg_num);
                // Save the new value of the register (it is restored later).
                memcpy(state->registers[frame] + reg_num,
                       (void *)opt_location_addr, loc_size);
                loc_index += 2;
            } else if (type == INDIRECT) {
                errx(1, "Not implemented - indirect.\n");
            } else if (type != CONSTANT && type != CONST_INDEX) {
                errx(1, "Unknown record - %u. Exiting\n", type);
            }
        }
    }
    free_locations(locations, num_locations);
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

void combine_states(call_stack_state_t *dest, call_stack_state_t *state,
                    stack_map_record_t first_rec)
{
    uint32_t new_depth = dest->depth;
    new_depth += state->depth;
    dest->ret_addrs = realloc(dest->ret_addrs, new_depth * sizeof(uint64_t));
    dest->bps = realloc(dest->bps, new_depth * sizeof(unw_word_t));
    dest->registers = realloc(dest->registers,
                              new_depth * sizeof(unw_word_t *));
    for (size_t i = dest->depth; i < new_depth; ++i) {
        dest->registers[i] = calloc(16, sizeof(unw_word_t));
    }
    append_record(dest, first_rec);
    memcpy(dest->records + dest->depth, state->records,
           state->depth * sizeof(stack_map_record_t));
    memcpy(dest->ret_addrs + dest->depth, state->ret_addrs,
           state->depth * sizeof(uint64_t));
    memcpy(dest->bps + dest->depth, state->bps,
           state->depth * sizeof(unw_word_t));
    memcpy(dest->registers + dest->depth, state->registers,
           state->depth * sizeof(unw_word_t*));
    dest->depth = new_depth;
    free(state);
}

