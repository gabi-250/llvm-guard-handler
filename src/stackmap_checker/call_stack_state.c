#include "call_stack_state.h"
#include "utils.h"
#include "stmap.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <assert.h>

#define MAX_BUF_SIZE 128

unw_word_t* get_registers(unw_cursor_t cursor)
{
    unw_word_t *registers = calloc(REGISTER_COUNT, sizeof(unw_word_t));
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
        frames = realloc(frames, ++depth * sizeof(frame_t));
        frames[depth - 1].registers = get_registers(cursor);
        // Store the address of the return address.
        frames[depth - 1].ret_addr =
            (uint64_t)(frames[depth - 1].registers[UNW_X86_64_RBP]
                       + ADDR_SIZE);
        frames[depth - 1].stored_ret_addr =
            *(uint64_t *)(frames[depth - 1].registers[UNW_X86_64_RBP]
                          + ADDR_SIZE);
        // Store the current BP.
        frames[depth - 1].bp = frames[depth - 1].real_bp =
            frames[depth - 1].registers[UNW_X86_64_RBP];
        frames[depth - 1].inlined = 0;
        // Stop when main is reached.
        char fun_name[MAX_BUF_SIZE];
        unw_get_proc_name(&cursor, fun_name, sizeof(fun_name), &off);
        if (!strcmp(fun_name, "main")) {
            break;
        }
    }
    state->frames = frames;
    state->depth  = depth;
    return state;
}

void collect_map_records(call_stack_state_t *state, stack_map_t *sm)
{
    for (size_t i = 0; i + 1 < state->depth; ++i) {
        // `sm_pos` identifies a position in a function. It is essentially an
        // address. A stack map record is always associated with a stack size
        // record. Each stack size record uniquely identifies a function, while
        // a stack map record contains the offset of the `stackmap` call in the
        // function. This position is located in an `__unopt_` function.
        stack_map_pos_t *sm_pos =
            stmap_get_unopt_return_addr(sm, *(uint64_t *)state->frames[i].ret_addr);
        uint64_t unopt_ret_addr =
            sm->stk_size_records[sm_pos->stk_size_record_index].fun_addr +
            sm->stk_map_records[sm_pos->stk_map_record_index].instr_offset +
            PATCHPOINT_CALL_SIZE;
        // The start address of the function in which this function returns.
        uint64_t fun_start_addr =
            get_sym_start(state->frames[i].stored_ret_addr);
        // Extract the identifier of the record.
        uint64_t ppid =
            ~sm->stk_map_records[sm_pos->stk_map_record_index].patchpoint_id;
        // The stack map record associated with this frame. Records are
        // duplicated when they are inlined (there may be more than one record
        // with the same identifier)
        stack_map_record_t *real_opt_stk_map_rec =
            stmap_get_map_record_after_addr(sm, ppid, state->frames[i].stored_ret_addr);
        stack_map_record_t *opt_stk_map_rec =
            stmap_get_map_record(
                sm,
                ~sm->stk_map_records[sm_pos->stk_map_record_index].patchpoint_id);
        // Overwrite the old return address.
        *(uint64_t *)state->frames[i].ret_addr = unopt_ret_addr;
        // Store each record that corresponds to a frame on the call stack.
        state->frames[i].record = *opt_stk_map_rec;
        state->frames[i].real_record = *real_opt_stk_map_rec;
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
    for (size_t i = index; i < index + num_frames; ++i) {
        state->frames[i].registers = calloc(REGISTER_COUNT,
                                            sizeof(unw_word_t));
        memcpy(state->frames[i].registers, frames[i - index].registers,
               REGISTER_COUNT * sizeof(unw_word_t));
    }
    state->depth = new_depth;
}

uint64_t get_next_patchpoint(stack_map_t *sm, uint64_t addr,
                             stack_size_record_t *size_rec)
{
    stack_map_record_t *rec = stmap_get_last_record(sm, *size_rec);
    uint64_t last_ppid = 0;
    while (rec) {
        last_ppid = rec->patchpoint_id;
        rec = stmap_get_map_record_in_func(sm, rec->patchpoint_id - 1,
                                           size_rec->fun_addr);
        if (rec) {
            size_rec = stmap_get_size_record(sm, rec->index);
            if (rec->instr_offset + size_rec->fun_addr < addr) {
                break;
            }
        }
    }
    return last_ppid;
}

bool collect_inlined_frames(call_stack_state_t *state, stack_map_t *sm)
{
    bool inlined = 0;
    call_stack_state_t *state_copy = get_state_copy(state);
    for (size_t i = 0; i + 1 < state_copy->depth; ++i) {
        stack_map_record_t record = state_copy->frames[i].record;
        // The function to which the record belongs
        stack_size_record_t *size_record =
            stmap_get_size_record(sm, record.index);
        // Find the last address of the function which contains this record
        uint64_t end_addr = get_sym_end(size_record->fun_addr);
        uint64_t callback_ret_addr =
            record.instr_offset + size_record->fun_addr;
        // Use the stored return address, instead of reading it from the
        // stack (`state_copy->frames[i].ret_addr` is the *address*
        // of the return address). Since `collect_map_records`
        // overwrites the return addresses on the stack, it is necessary
        // to use the stored value instead.
        uint64_t ret_addr = state_copy->frames[i].stored_ret_addr;
        if (!(ret_addr >= size_record->fun_addr &&
            ret_addr < (uint64_t)end_addr)) {
            // Frame with index i does not return in the function associated
            // with the frame at index i + 1, so it corresponds to an
            // inlined function.
            inlined = 1;
            uint64_t next_addr = ret_addr + PATCHPOINT_CALL_SIZE + 1;
            // Get the size record of the function in which inlining happened.
            stack_size_record_t *real_size_record =
                stmap_get_size_record_in_func(sm, get_sym_start(next_addr));
            uint64_t inlined_patchpoint_addr =
                real_size_record->fun_addr + record.instr_offset;
            // The next 'real' PPID in the function. We only want to collect
            // records up to this patchpoint, because this patchpoint
            // actually belongs to the function, i.e. it wasn't 'inserted'
            // in the function through inlining.
            uint64_t last_ppid = get_next_patchpoint(sm,
                                                     inlined_patchpoint_addr,
                                                     real_size_record);
            call_stack_state_t *res_state =
                get_restored_state(sm, next_addr, last_ppid);
            // Copy all the data from the function in which this record
            // was inlined.
            for (size_t j = 0; j < res_state->depth; ++j) {
                // The real base pointer of this function is that of the
                // function it was inlined in
                res_state->frames[j].real_bp =
                    state_copy->frames[i + 1].real_bp;
                res_state->frames[j].registers =
                    calloc(REGISTER_COUNT, sizeof(unw_word_t));
                // The registers of the frames which correspond to inlined
                // functions have the same values as the registers of the
                // frame they were inlined in.
                memcpy(res_state->frames[j].registers,
                       state_copy->frames[i + 1].registers,
                       REGISTER_COUNT * sizeof(unw_word_t));
            }
            // Insert the restored frames inside `state`.
            insert_frames(state, i + 1, res_state->frames, res_state->depth);
            free_call_stack_state(res_state);
        }
    }
    free(state_copy);
    return inlined;
}

call_stack_state_t* get_restored_state(stack_map_t *sm,
                                       uint64_t start_addr,
                                       uint64_t ppid)
{
    call_stack_state_t *state = malloc(sizeof(call_stack_state_t));
    state->depth = 0;
    state->frames = NULL;
    stack_map_record_t *rec = NULL;
    // The stack map records which correspond to each call on the stack.
    // The number of frames on the stack.
    // Find the 'return addresses' of the inlined functions
    // The next 'real' record in this function.
    uint64_t opt_ret_addr = start_addr;
    do {
        rec = stmap_first_rec_after_addr(sm, opt_ret_addr);
        if (!rec) {
            break;
        }
        stack_size_record_t *size_rec = stmap_get_size_record(sm, rec->index);
        if (!size_rec) {
            errx(1, "Size record not found\n");
        }
        stack_map_record_t *unopt_rec =
            stmap_get_map_record(sm, ~rec->patchpoint_id);
        stack_size_record_t *unopt_size_rec =
            stmap_get_size_record(sm, unopt_rec->index);
        ++state->depth;
        state->frames = realloc(state->frames, state->depth * sizeof(frame_t));
        // ret_addr does not hold the address of the return address
        // if inlined = 1 (it stores the return address instead).
        state->frames[state->depth - 1].ret_addr =
            unopt_rec->instr_offset + unopt_size_rec->fun_addr + 13;
        opt_ret_addr = rec->instr_offset + size_rec->fun_addr + 1;
        state->frames[state->depth - 1].size = unopt_size_rec->stack_size;
        state->frames[state->depth - 1].record = *rec;
        state->frames[state->depth - 1].real_record =
            *stmap_get_map_record_after_addr(sm, rec->patchpoint_id,
                                             start_addr);
        state->frames[state->depth - 1].inlined = 1;
    } while(rec->patchpoint_id != ppid);
    return state;
}

void insert_real_addresses(call_stack_state_t *state, restored_segment_t seg)
{
    // Now add the return addresses and saved base pointers to the new 'stack'.
    // Start with the low addresses: `cur_bp` is the BP of the last function
    // callled
    char *cur_bp = (char *)seg.start_addr + state->frames[0].size - ADDR_SIZE;
    for (size_t i = 0; i + 1 < state->depth; ++i) {
        state->frames[i].bp = (uint64_t)cur_bp;
        if (state->frames[i + 1].inlined) {
            *(uint64_t *)(cur_bp + ADDR_SIZE) = state->frames[i + 1].ret_addr;
        } else {
            *(uint64_t *)(cur_bp + ADDR_SIZE) =
                *(uint64_t *)state->frames[i + 1].ret_addr;
        }
        state->frames[i].ret_addr = (uint64_t)(cur_bp + ADDR_SIZE);
        if (i > 0) {
            *(uint64_t *)state->frames[i - 1].bp = state->frames[i].bp;
        }
        cur_bp += state->frames[i + 1].size + ADDR_SIZE;
    }
    // Link the restored stack frames with the frame of main.
    uint64_t main_bp =
        state->frames[state->depth - 1].registers[UNW_X86_64_RBP];
    uint64_t main_ret_addr = (uint64_t)(main_bp + ADDR_SIZE);
    *(uint64_t *)state->frames[state->depth - 1].bp = main_bp;
    *(uint64_t *)state->frames[state->depth - 1].ret_addr =
        *(uint64_t *)main_ret_addr;
}

size_t get_locations(stack_map_t *sm, call_stack_state_t *state,
                     uint64_t **locs)
{
    size_t num_locations = 0;
    size_t loc_index = 0;
    // Each record corresponds to a stack frame.
    for (size_t i = 0; i + 1 < state->depth; ++i) {
        stack_map_record_t opt_rec = state->frames[i].real_record;
        stack_map_record_t *unopt_rec = stmap_get_map_record(
                sm, ~opt_rec.patchpoint_id);
        assert(opt_rec.num_locations == unopt_rec->num_locations);
        uint64_t real_bp = state->frames[i + 1].real_bp;
        num_locations += unopt_rec->num_locations;
        *locs = (uint64_t *)realloc(*locs, num_locations * sizeof(uint64_t));
        // Populate the stack of the optimized function with the values the
        // unoptimized function expects
        for (size_t j = 0; j + 1 < opt_rec.num_locations; j += 2) {
            // First, retrieve the size of the location at index `j`. This will
            // be stored in `loc_size`. The size of the location which
            // represents the size of location `j` is a `uint64_t` value,
            // because `LiveVariablesPass` records location sizes as 64-bit
            // values.
            uint64_t *loc_size =
                stmap_get_location_value(sm, opt_rec.locations[j + 1],
                                         state->frames[i].registers,
                                         (void *)real_bp,
                                         sizeof(uint64_t));
            // Now, copy `loc_size` bytes starting at the address indicated by
            // the location at position `j`.
            void *opt_location_value =
                stmap_get_location_value(sm, opt_rec.locations[j],
                                         state->frames[i].registers,
                                         (void *)real_bp,
                                         *loc_size);
            uint64_t location_value_addr = (uint64_t) opt_location_value;
            uint64_t loc_size_addr = (uint64_t) loc_size;
            memcpy(*locs + loc_index++, &location_value_addr, sizeof(uint64_t));
            memcpy(*locs + loc_index++, &loc_size_addr, sizeof(uint64_t));
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
    size_t loc_index = 0;
    // Restore all the stacks on the call stack
    for (size_t i = 0; i + 1 < state->depth; ++i) {
        // Get the unoptimized stack map record associated with this frame.
        stack_map_record_t *unopt_rec =
            stmap_get_map_record(sm, ~state->frames[i].record.patchpoint_id);
        uint64_t bp = state->frames[i].bp;
        // Populate the stack of the optimized function with the values the
        // unoptimized function expects.
        // Records are considered in pairs (the counter is incremented by 2),
        // because each record at an odd index in the array represents the
        // size of the previous record.
        for (size_t j = 0; j + 1 < unopt_rec->num_locations; j += 2) {
            location_type type = unopt_rec->locations[j].kind;
            uint64_t opt_location_addr = locations[loc_index++];
            uint64_t loc_size = *(uint64_t *)locations[loc_index++];
            if (type == DIRECT) {
                uint64_t unopt_addr = bp + unopt_rec->locations[j].offset;
                memcpy((void *)unopt_addr, (void *)opt_location_addr,
                       loc_size);
            } else if (type == REGISTER) {
                uint16_t reg_num = unopt_rec->locations[j].dwarf_reg_num;
                assert_valid_reg_num(reg_num);
                // Save the new value of the register (it is restored later).
                memcpy(state->frames[i].registers + reg_num,
                       (void *)opt_location_addr, sizeof(unw_word_t));
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
    for (size_t i = 0; i < REGISTER_COUNT; ++i) {
        r[i] = (uint64_t)state->frames[0].registers[i];
    }
}

frame_t* alloc_empty_frames(size_t num_frames)
{
    frame_t *frames = calloc(num_frames, sizeof(frame_t));
    for (size_t i = 0; i < num_frames; ++i) {
        frames[i].registers = calloc(REGISTER_COUNT, sizeof(unw_word_t));
    }
    return frames;
}


void free_frames(frame_t *frames, size_t num_frames)
{
    for (size_t i = 0; i < num_frames; ++i) {
        free(frames[i].registers);
    }
    free(frames);
}

uint64_t get_total_stack_size(call_stack_state_t *state)
{
    uint64_t total_size = 0;
    for (size_t i = 0; i + 1 < state->depth; ++i) {
        // Add ADDR_SIZE to account for the size of the return address.
        total_size += state->frames[i].size + ADDR_SIZE;
    }
    return total_size;
}
