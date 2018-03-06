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
    call_stack_state_t *state = malloc(sizeof(call_stack_state_t));
    frame_t *frames = NULL;
    uint32_t depth = 0;
    while (unw_step(&cursor) > 0) {
        unw_word_t off, pc;
        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (!pc) {
            break;
        }
        // Stop when main is reached.
        char fun_name[MAX_BUF_SIZE];
        unw_get_proc_name(&cursor, fun_name, sizeof(fun_name), &off);
        if (!strcmp(fun_name, "main")) {
            unw_word_t rbp;
            unw_get_reg(&cursor, UNW_X86_64_RBP, &rbp);
            state->main_ret_addr = (uint64_t)(rbp + 8);
            state->main_bp = (uint64_t) rbp;
            state->main_regs = calloc(16, sizeof(unw_word_t));
            unw_get_reg(&cursor, UNW_X86_64_RAX, &state->main_regs[0]);
            unw_get_reg(&cursor, UNW_X86_64_RDX, &state->main_regs[1]);
            unw_get_reg(&cursor, UNW_X86_64_RCX, &state->main_regs[2]);
            unw_get_reg(&cursor, UNW_X86_64_RBX, &state->main_regs[3]);
            unw_get_reg(&cursor, UNW_X86_64_RSI, &state->main_regs[4]);
            unw_get_reg(&cursor, UNW_X86_64_RDI, &state->main_regs[5]);
            unw_get_reg(&cursor, UNW_X86_64_RBP, &state->main_regs[6]);
            unw_get_reg(&cursor, UNW_X86_64_RSP, &state->main_regs[7]);
            unw_get_reg(&cursor, UNW_X86_64_R8,  &state->main_regs[8]);
            unw_get_reg(&cursor, UNW_X86_64_R9,  &state->main_regs[9]);
            unw_get_reg(&cursor, UNW_X86_64_R10, &state->main_regs[10]);
            unw_get_reg(&cursor, UNW_X86_64_R11, &state->main_regs[11]);
            unw_get_reg(&cursor, UNW_X86_64_R12, &state->main_regs[12]);
            unw_get_reg(&cursor, UNW_X86_64_R13, &state->main_regs[13]);
            unw_get_reg(&cursor, UNW_X86_64_R14, &state->main_regs[14]);
            unw_get_reg(&cursor, UNW_X86_64_R15, &state->main_regs[15]);
            break;
        }
        // Always allocate an extra frame
        frames = realloc(frames, (depth + 1) * sizeof(frame_t));
        frames[depth].registers = calloc(16, sizeof(unw_word_t));
        unw_get_reg(&cursor, UNW_X86_64_RAX, &frames[depth].registers[0]);
        unw_get_reg(&cursor, UNW_X86_64_RDX, &frames[depth].registers[1]);
        unw_get_reg(&cursor, UNW_X86_64_RCX, &frames[depth].registers[2]);
        unw_get_reg(&cursor, UNW_X86_64_RBX, &frames[depth].registers[3]);
        unw_get_reg(&cursor, UNW_X86_64_RSI, &frames[depth].registers[4]);
        unw_get_reg(&cursor, UNW_X86_64_RDI, &frames[depth].registers[5]);
        unw_get_reg(&cursor, UNW_X86_64_RBP, &frames[depth].registers[6]);
        unw_get_reg(&cursor, UNW_X86_64_RSP, &frames[depth].registers[7]);
        unw_get_reg(&cursor, UNW_X86_64_R8,  &frames[depth].registers[8]);
        unw_get_reg(&cursor, UNW_X86_64_R9,  &frames[depth].registers[9]);
        unw_get_reg(&cursor, UNW_X86_64_R10, &frames[depth].registers[10]);
        unw_get_reg(&cursor, UNW_X86_64_R11, &frames[depth].registers[11]);
        unw_get_reg(&cursor, UNW_X86_64_R12, &frames[depth].registers[12]);
        unw_get_reg(&cursor, UNW_X86_64_R13, &frames[depth].registers[13]);
        unw_get_reg(&cursor, UNW_X86_64_R14, &frames[depth].registers[14]);
        unw_get_reg(&cursor, UNW_X86_64_R15, &frames[depth].registers[15]);
        // Store the address of the return address.
        frames[depth].ret_addr =
            (uint64_t)(frames[depth].registers[UNW_X86_64_RBP] + 8);
        // Store the current BP.
        frames[depth].bp = frames[depth].bp2 = frames[depth].registers[UNW_X86_64_RBP];
        ++depth;
    }
    state->frames = frames;
    state->depth  = depth;
    return state;
}

void collect_map_records(call_stack_state_t *state, stack_map_t *sm)
{
    for (size_t i = 0; i < state->depth; ++i) {
        // `sm_pos` identifies a position in a function. It is essentially an
        // address. A stack map record is always associated with a stack size
        // record. Each stack size record uniquely identifies a function, while
        // a stack map record contains the offset of the `stackmap` call in the
        // function.
        stack_map_pos_t *sm_pos =
            stmap_get_unopt_return_addr(sm, *(uint64_t *)state->frames[i].ret_addr);
        uint64_t unopt_ret_addr =
            sm->stk_size_records[sm_pos->stk_size_record_index].fun_addr +
            sm->stk_map_records[sm_pos->stk_map_record_index].instr_offset +
            PATCHPOINT_CALL_SIZE;
        // Overwrite the old return addresses
        *(uint64_t *)state->frames[i].ret_addr = unopt_ret_addr;
        // The stack map record associated with this frame.
        stack_map_record_t *opt_stk_map_rec =
            stmap_get_map_record(
                sm,
                ~sm->stk_map_records[sm_pos->stk_map_record_index].patchpoint_id);
        // Store each record that corresponds to a frame on the call stack.
        if (i + 1 < state->depth) {
            state->frames[i + 1].record = *opt_stk_map_rec;
            // XXX which stack size?
            stack_size_record_t *opt_size_rec =
                stmap_get_size_record(sm, opt_stk_map_rec->index);
            state->frames[i].size = opt_size_rec->stack_size;
        }
        free(sm_pos);
    }
}

void free_call_stack_state(call_stack_state_t *state)
{
    for(size_t i = 0; i < state->depth; ++i) {
        free(state->frames[i].registers);
    }
    free(state->frames);
    free(state);
}

call_stack_state_t* get_restored_state(stack_map_t *sm, uint64_t ppid,
    uint64_t start_addr, uint64_t end_addr)
{
    call_stack_state_t *restored_state = malloc(sizeof(call_stack_state_t));
    frame_t *frames = NULL;
    stack_map_record_t *rec = stmap_first_rec_after_addr(sm, start_addr);
    stack_size_record_t *size_rec = stmap_get_size_record(sm, rec->index);
    // The stack map records which correspond to each call on the stack.
    // The number of frames on the stack.
    uint32_t depth = 0;
    // Find the 'return addresses' of the inlined functions
    // XXX
    uint64_t last_max = 0;
    while(rec->instr_offset + size_rec->fun_addr != end_addr) {
        stack_map_record_t *unopt_rec =
            stmap_get_map_record(sm, ~rec->patchpoint_id);
        stack_size_record_t *unopt_size_rec =
            stmap_get_size_record(sm, unopt_rec->index);
        frames = realloc(frames, (depth + 2) * sizeof(frame_t));
        frames[depth].registers     = calloc(16, sizeof(unw_word_t));
        // XXX ret_addr does not hold the address of the return address
        // should be stored in a separate array
        frames[depth].ret_addr =
            unopt_rec->instr_offset + unopt_size_rec->fun_addr + 13;
        uint64_t opt_ret_addr = rec->instr_offset + size_rec->fun_addr + 1;
        ++depth;
        frames[depth].size      = unopt_size_rec->stack_size;
        frames[depth].record    = *rec;
        last_max = size_rec->stack_size > last_max ? size_rec->stack_size : last_max;
        last_max = unopt_size_rec->stack_size > last_max ? unopt_size_rec->stack_size
            : last_max;
        rec = stmap_first_rec_after_addr(sm, opt_ret_addr);
        if (!rec) {
            break;
        }
        size_rec = stmap_get_size_record(sm, rec->index);
        if (!size_rec) {
            errx(1, "Size record not found\n");
        }
    }
    restored_state->depth  = depth + 1; // + 1 leak
    frames[restored_state->depth - 1].registers = calloc(16, sizeof(unw_word_t));
    frames[restored_state->depth - 1].size = last_max;
    restored_state->frames = frames;
    return restored_state;
}

void insert_real_addresses(call_stack_state_t *state, restored_segment_t seg,
        uint64_t last_bp, uint64_t last_ret_addr)
{
    // now add the return addresses to the new 'stack' and save the bps
    // start with the high address
    char *cur_bp = (char *) seg.start_addr;// + seg.total_size;
    for (size_t i = 0; i < state->depth; ++i) {
        // returns in __unopt_more_indirection, where the PP failed
        // must add that as the first stack size
        state->frames[i].bp = (uint64_t)cur_bp;
        *(uint64_t *)(cur_bp + 8) = state->frames[i].ret_addr;
        state->frames[i].ret_addr = (uint64_t)(cur_bp + 8);
        state->frames[i].registers = calloc(16, sizeof(unw_word_t));
        if (i > 0) {
            *(uint64_t *)state->frames[i - 1].bp = state->frames[i].bp;
        }
        cur_bp += 8;
        if (i + 1 < state->depth) {
            cur_bp += state->frames[i + 1].size;
        }
    }
    // Link the restored stack frames with the frames of the non-inlined
    // functions.
    *(uint64_t *)state->frames[state->depth - 1].bp = last_bp;
    *(uint64_t *)state->frames[state->depth - 1].ret_addr = last_ret_addr;
}

size_t get_locations(stack_map_t *sm, call_stack_state_t *state,
                     uint64_t **locs)
{
    size_t num_locations = 0;
    size_t loc_index = 0;
    size_t new_size = 0;
    // Each record corresponds to a stack frame.
    for (size_t i = 0; i < state->depth; ++i) {
        stack_map_record_t opt_rec = state->frames[i].record;
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
                                     state->frames[i].registers,
                                     (void *)state->frames[i].bp2,
                                     (void *)&loc_size, sizeof(uint64_t));
            // Now, copy `loc_size` bytes starting at the address indicated by
            // the location at position `j`.
            stmap_get_location_value(sm, opt_rec.locations[j],
                                     state->frames[i].registers,
                                     (void *)state->frames[i].bp2,
                                     &opt_location_value,
                                     *loc_size);
            uint64_t location_value_addr = (uint64_t) opt_location_value;
            uint64_t loc_size_addr = (uint64_t) loc_size;
            memcpy(*locs + loc_index, &location_value_addr, sizeof(uint64_t));
            ++loc_index;
            memcpy(*locs + loc_index, &loc_size_addr, sizeof(uint64_t));
            ++loc_index;
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
    for (int i = 0; i < state->depth; ++i) {
        // Get the unoptimized stack map record associated with this frame.
        stack_map_record_t *unopt_rec = stmap_get_map_record(sm,
                ~state->frames[i].record.patchpoint_id);
        // Populate the stack of the optimized function with the values the
        // unoptimized function expects.
        // Records are considered in pairs (the counter is incremented by 2),
        // because each record at an odd index in the array represents the
        // size of the previous record.
        for (int j = 0; j < unopt_rec->num_locations - 1; j += 2) {
            location_type type = unopt_rec->locations[j].kind;
            uint64_t opt_location_addr = locations[loc_index++];
            uint64_t loc_size = *(uint64_t *)locations[loc_index++];
            if (type == DIRECT) {
                uint64_t unopt_addr = (uint64_t)state->frames[i].bp +
                    unopt_rec->locations[j].offset;
                memcpy((void *)unopt_addr, (void *)opt_location_addr,
                        loc_size);
            } else if (type == REGISTER) {
                uint16_t reg_num = unopt_rec->locations[j].dwarf_reg_num;
                assert_valid_reg_num(reg_num);
                // Save the new value of the register (it is restored later).
                memcpy(state->frames[i].registers + reg_num,
                       (void *)opt_location_addr, loc_size);
            } else if (type == INDIRECT) {
                errx(1, "Not implemented - indirect.\n");
            } else if (type != CONSTANT && type != CONST_INDEX) {
                errx(1, "Unknown record - %u. Exiting\n", type);
            }
        }
    }
    free_locations(locations, num_locations);
}

void restore_register_state(call_stack_state_t *state, uint64_t r[])
{
    for (size_t i = 0; i < 16; ++i) {
        r[i] = (uint64_t)state->frames[0].registers[i];
    }
}

void combine_states(call_stack_state_t *dest, call_stack_state_t *state)
{
    for (size_t i = 0; i < dest->depth; ++i) {
        for (size_t j = 0; j < 16; ++j) {
            if (!state->depth) {
                dest->frames[i].registers[j] = state->main_regs[j];
            } else {
                dest->frames[i].registers[j] = state->frames[0].registers[j];
            }
        }
        if (!state->depth) {
            dest->frames[i].bp2 = state->main_bp;
        } else {
            dest->frames[i].bp2 = state->frames[0].bp;
        }
    }
    for (size_t i = 0; i < state->depth; ++i) {
        state->frames[i].bp2 = state->frames[0].bp;
        // XXX hack to be fixed
        state->frames[i].ret_addr = *(uint64_t *)state->frames[i].ret_addr;
    }
    if (state->depth) {
        dest->frames[dest->depth - 1].ret_addr = state->frames[0].ret_addr;
        // XXX
        free(state->frames[0].registers);
        memmove(state->frames, state->frames + 1,
                (state->depth - 1) * sizeof(frame_t));

        --state->depth;
    }
    uint32_t new_depth = dest->depth + state->depth;
    dest->frames = realloc(dest->frames, new_depth * sizeof(frame_t));
    memcpy(dest->frames + dest->depth, state->frames,
           state->depth * sizeof(frame_t));
    dest->depth = new_depth;
    free(state);
}

