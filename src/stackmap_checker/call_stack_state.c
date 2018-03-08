#include "call_stack_state.h"
#include "utils.h"
#include "stmap.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#define MAX_BUF_SIZE 128

unw_word_t* get_registers(unw_cursor_t cursor)
{
    unw_word_t *registers = calloc(16, sizeof(unw_word_t));
    unw_get_reg(&cursor, UNW_X86_64_RAX, &registers[0]);
    unw_get_reg(&cursor, UNW_X86_64_RDX, &registers[1]);
    unw_get_reg(&cursor, UNW_X86_64_RCX, &registers[2]);
    unw_get_reg(&cursor, UNW_X86_64_RBX, &registers[3]);
    unw_get_reg(&cursor, UNW_X86_64_RSI, &registers[4]);
    unw_get_reg(&cursor, UNW_X86_64_RDI, &registers[5]);
    unw_get_reg(&cursor, UNW_X86_64_RBP, &registers[6]);
    unw_get_reg(&cursor, UNW_X86_64_RSP, &registers[7]);
    unw_get_reg(&cursor, UNW_X86_64_R8,  &registers[8]);
    unw_get_reg(&cursor, UNW_X86_64_R9,  &registers[9]);
    unw_get_reg(&cursor, UNW_X86_64_R10, &registers[10]);
    unw_get_reg(&cursor, UNW_X86_64_R11, &registers[11]);
    unw_get_reg(&cursor, UNW_X86_64_R12, &registers[12]);
    unw_get_reg(&cursor, UNW_X86_64_R13, &registers[13]);
    unw_get_reg(&cursor, UNW_X86_64_R14, &registers[14]);
    unw_get_reg(&cursor, UNW_X86_64_R15, &registers[15]);
    return registers;
}

call_stack_state_t* get_call_stack_state(unw_cursor_t cursor)
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
            state->main_regs = get_registers(cursor);
            state->main_bp = state->main_regs[UNW_X86_64_RBP];
            state->main_ret_addr = (uint64_t)(state->main_bp + 8);
            break;
        }
        frames = realloc(frames, ++depth * sizeof(frame_t));
        frames[depth - 1].registers = get_registers(cursor);
        // Store the address of the return address.
        frames[depth - 1].ret_addr =
            (uint64_t)(frames[depth - 1].registers[UNW_X86_64_RBP] + 8);
        frames[depth - 1].opt_ret_addr =
            *(uint64_t *)(frames[depth - 1].registers[UNW_X86_64_RBP] + 8);
        // Store the current BP.
        frames[depth - 1].bp = frames[depth - 1].real_bp =
            frames[depth - 1].registers[UNW_X86_64_RBP];
        frames[depth - 1].inlined = 0;
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
        state->frames[i].record = *opt_stk_map_rec;
        // XXX which stack size?
        stack_size_record_t *opt_size_rec =
            stmap_get_size_record(sm, opt_stk_map_rec->index);
        state->frames[i].size = opt_size_rec->stack_size;
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

call_stack_state_t* get_state_copy(call_stack_state_t *state) {
    call_stack_state_t *copy = malloc(sizeof(call_stack_state_t));
    memcpy(copy, state, sizeof(call_stack_state_t));
    return copy;
}

void insert_frames(call_stack_state_t *state, size_t index,
                   frame_t *frames, size_t num_frames)
{
    uint32_t new_depth = state->depth + num_frames;
    state->frames = realloc(state->frames, new_depth * sizeof(frame_t));
    memmove(state->frames + index + num_frames, state->frames + index,
            (state->depth - index) * sizeof(frame_t));
    memcpy(state->frames + index, frames, num_frames * sizeof(frame_t));
    state->depth = new_depth;
}

void collect_inlined_frames(call_stack_state_t *state, stack_map_t *sm)
{
    char *binary_path = get_binary_path();
    call_stack_state_t *state_copy = get_state_copy(state);
    for (size_t i = 0; i < state_copy->depth; ++i) {
        stack_map_record_t *record =
                &state_copy->frames[i].record;
        stack_size_record_t *size_record = stmap_get_size_record(sm, record->index);
        void *end_addr = get_sym_end(binary_path, (void *)size_record->fun_addr);
        uint64_t callback_ret_addr = record->instr_offset + size_record->fun_addr;
        if (state_copy->frames[i].opt_ret_addr >= size_record->fun_addr &&
            state_copy->frames[i].opt_ret_addr < (uint64_t)end_addr) {
            fprintf(stderr, "Regular frame!\n");
        } else {
            fprintf(stderr, "Inlined!\n");
            stack_map_record_t *record = &state_copy->frames[i - 1].record;
            stack_size_record_t *size_record =
                stmap_get_size_record(sm, record->index);
            uint64_t opt_ret_addr = size_record->fun_addr + record->instr_offset;
            void *end_addr = get_sym_end(binary_path, (void *)size_record->fun_addr);
            size_record =
                stmap_get_size_record(sm, state_copy->frames[i].record.index);
            end_addr = get_sym_end(binary_path, (void *)size_record->fun_addr);
            uint64_t next_addr =
                state_copy->frames[i].opt_ret_addr + PATCHPOINT_CALL_SIZE + 1;
            call_stack_state_t *res_state =
                get_restored_state(sm, next_addr, (uint64_t)end_addr);
            for (size_t j = 0; j < res_state->depth; ++j) {
                // XXX
                res_state->frames[j].real_bp = state->frames[i].bp;
            }
            insert_frames(state, i + 1, res_state->frames, res_state->depth);
        }
    }
    free(state_copy);
    free(binary_path);
}

call_stack_state_t* get_restored_state(stack_map_t *sm, uint64_t start_addr,
                                       uint64_t end_addr)
{
    call_stack_state_t *state = malloc(sizeof(call_stack_state_t));
    state->depth = 0;
    state->frames = NULL;
    stack_map_record_t *rec = stmap_first_rec_after_addr(sm, start_addr);
    stack_size_record_t *size_rec = stmap_get_size_record(sm, rec->index);
    // The stack map records which correspond to each call on the stack.
    // The number of frames on the stack.
    // Find the 'return addresses' of the inlined functions
    while(rec->instr_offset + size_rec->fun_addr != end_addr) {
        stack_map_record_t *unopt_rec =
            stmap_get_map_record(sm, ~rec->patchpoint_id);
        stack_size_record_t *unopt_size_rec =
            stmap_get_size_record(sm, unopt_rec->index);
        ++state->depth;
        state->frames = realloc(state->frames, state->depth * sizeof(frame_t));
        state->frames[state->depth - 1].registers =
            calloc(16, sizeof(unw_word_t));
        // XXX ret_addr does not hold the address of the return address
        // should be stored in a separate array
        state->frames[state->depth - 1].ret_addr =
            unopt_rec->instr_offset + unopt_size_rec->fun_addr + 13;
        uint64_t opt_ret_addr = rec->instr_offset + size_rec->fun_addr + 1;
        state->frames[state->depth - 1].size =
            unopt_size_rec->stack_size;
        state->frames[state->depth - 1].record = *rec;
        state->frames[state->depth - 1].inlined = 1;
        rec = stmap_first_rec_after_addr(sm, opt_ret_addr);
        if (!rec) {
            break;
        }
        size_rec = stmap_get_size_record(sm, rec->index);
        if (!size_rec) {
            errx(1, "Size record not found\n");
        }
    }
    return state;
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
        if (state->frames[i].inlined) {
            *(uint64_t *)(cur_bp + 8) = state->frames[i].ret_addr;
        } else {
            *(uint64_t *)(cur_bp + 8) = *(uint64_t *)state->frames[i].ret_addr;
        }
        state->frames[i].ret_addr = (uint64_t)(cur_bp + 8);
        state->frames[i].registers = calloc(16, sizeof(unw_word_t));
        if (i > 0) {
            *(uint64_t *)state->frames[i - 1].bp = state->frames[i].bp;
        }
        cur_bp += 8;
        if (i < state->depth) {
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
                                     (void *)state->frames[i].real_bp,
                                     (void *)&loc_size, sizeof(uint64_t));
            // Now, copy `loc_size` bytes starting at the address indicated by
            // the location at position `j`.
            stmap_get_location_value(sm, opt_rec.locations[j],
                                     state->frames[i].registers,
                                     (void *)state->frames[i].real_bp,
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

