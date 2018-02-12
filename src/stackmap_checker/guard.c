#define UNW_LOCAL_ONLY

#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <signal.h>
#include <libunwind.h>
#include "stmap.h"
#include "utils.h"

#define MAX_CALL_STACK_DEPTH 256

typedef struct CallStackState {
    uint64_t *ret_addrs;
    unw_word_t *bps;
    unw_word_t **registers;
    stack_map_record_t *records;
    uint32_t depth;
} call_stack_state_t;

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

        // Find base pointer of each function.
        char fun_name[128];
        unw_get_proc_name(&cursor, fun_name, sizeof(fun_name), &off);
        printf("Found ret addr %p\n",
            *(uint64_t *)((char *)registers[frame][6] + 8));
        ret_addrs[frame] =
            (uint64_t)((char *)registers[frame][UNW_X86_64_RBP] + 8);
        *(bps + frame) = registers[frame][UNW_X86_64_RBP];
        frame++;
        if (!strcmp(fun_name, "main")) {
            --frame;
            free (registers[frame]);
            break;
        }
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

// the label to jump to whenever a guard fails
extern void restore_and_jmp(void);
uint64_t addr = 0;

int get_direct_locations(stack_map_t *sm, call_stack_state_t *state,
        uint64_t **direct_locs)
{
    int num_locations = 0;
    uint64_t new_size = 0;
    // Each record corresponds to a stack frame.
    for (int i = 0; i < state->depth; ++i) {
        stack_map_record_t opt_rec = state->records[i];
        stack_map_record_t *unopt_rec =
            stmap_get_map_record(sm, ~opt_rec.patchpoint_id);
        new_size += opt_rec.num_locations * sizeof(uint64_t);
        *direct_locs = (uint64_t *)realloc(*direct_locs, new_size);
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
                printf("Direct\n");
                memcpy(*direct_locs + num_locations, &opt_location_value,
                       loc_size);
                ++num_locations;
                memcpy(*direct_locs + num_locations, &loc_size, sizeof(uint64_t));
                ++num_locations;
            } else if (type == REGISTER) {
                memcpy(*direct_locs + num_locations, &opt_location_value,
                       loc_size);
                ++num_locations;
                memcpy(*direct_locs + num_locations, &loc_size, sizeof(uint64_t));
                ++num_locations;
            } else if (type == INDIRECT) {
                uint64_t unopt_addr = (uint64_t)state->bps[i] +
                    unopt_rec->locations[j].offset;
                errx(1, "Not implemented - indirect.\n");
            } else if (type != CONSTANT && type != CONST_INDEX && type != REGISTER) {
                errx(1, "Unknown record - %u. Exiting\n", type);
            }
        }
    }
    return num_locations;
}

void print_registers(size_t frame, unw_word_t **registers)
{
    printf("Registers in frame %d\n", frame);
    printf("\tRAX: %lu\n", registers[frame][UNW_X86_64_RAX]);
    printf("\tRCX: %lu\n", registers[frame][UNW_X86_64_RCX]);
    printf("\tRDX: %lu\n", registers[frame][UNW_X86_64_RDX]);
    printf("\tRBX: %lu\n", registers[frame][UNW_X86_64_RBX]);
    printf("\tRSP: %lu\n", registers[frame][UNW_X86_64_RSP]);
    printf("\tRBP: %lu\n", registers[frame][UNW_X86_64_RBP]);
    printf("\tRSI: %lu\n", registers[frame][UNW_X86_64_RSI]);
    printf("\tRDI: %lu\n", registers[frame][UNW_X86_64_RDI]);
    printf("\tR8: %lu\n", registers[frame][UNW_X86_64_R8]);
    printf("\tR9: %lu\n", registers[frame][UNW_X86_64_R9]);
    printf("\tR10: %lu\n", registers[frame][UNW_X86_64_R10]);
    printf("\tR11: %lu\n", registers[frame][UNW_X86_64_R11]);
    printf("\tR12: %lu\n", registers[frame][UNW_X86_64_R12]);
    printf("\tR13: %lu\n", registers[frame][UNW_X86_64_R13]);
    printf("\tR14: %lu\n", registers[frame][UNW_X86_64_R14]);
    printf("\tR15: %lu\n", registers[frame][UNW_X86_64_R15]);
}

void print_return_addrs(uint64_t **ret_addrs, size_t call_stack_depth)
{
    for (int frame = 0; frame < call_stack_depth; ++frame) {
        printf("* Ret addr %p @ %p\n", (void *)*(uint64_t *)ret_addrs[frame],
                ret_addrs[frame]);
    }
}

void restore_unopt_stack(stack_map_t *sm, call_stack_state_t *state,
                         stack_map_record_t *unopt_rec,
                         stack_map_record_t *opt_rec)
{
    // Need call_stack_depth + 1 stack maps (1 = the patchpoint which failed);
    // This stores the stack map records of the unoptimized trace
    stack_map_record_t *unopt_call_stk_records = calloc(state->depth,
                                                  sizeof(stack_map_record_t));

    state->records = calloc(state->depth, sizeof(stack_map_record_t));


    // Overwrite the old return addresses and store the stack map records
    // that correspond to each call which generated this stack trace in
    // unopt_call_stk_records.
    unopt_call_stk_records[0] = *unopt_rec;
    state->records[0] = *opt_rec;
    stmap_print_map_record(sm, opt_rec->index, state->registers[0],
                           (void *)state->bps[0]);
    // Loop depth - 1 times, because the return address of 'trace'
    // should not be overwritten (it should still return in main)
    for (int i = 0; i < state->depth - 1; ++i) {
        stack_map_pos_t *sm_pos =
            stmap_get_unopt_return_addr(sm, *(uint64_t *)state->ret_addrs[i]);
        uint64_t unopt_ret_addr =
            sm->stk_size_records[sm_pos->stk_size_record_index].fun_addr +
            sm->stk_map_records[sm_pos->stk_map_record_index].instr_offset;
        // Overwrite the old return addresses
        *(uint64_t *)state->ret_addrs[i] = unopt_ret_addr;
        stack_map_record_t *opt_stk_map_rec =
            stmap_get_map_record(
                sm,
                ~sm->stk_map_records[sm_pos->stk_map_record_index].patchpoint_id);
        unopt_call_stk_records[i + 1] =
            sm->stk_map_records[sm_pos->stk_map_record_index];

        state->records[i + 1] = *opt_stk_map_rec;
        free(sm_pos);
    }

    uint64_t *direct_locations = NULL;
    int num_locations = get_direct_locations(sm, state, &direct_locations);

    int loc_index = 0;
    // Restore all the stacks on the call stack
    for (int frame = 0; frame < state->depth; ++frame) {
        stack_map_record_t unopt_rec = unopt_call_stk_records[frame];

        stmap_print_map_record(sm, unopt_rec.index,
                               state->registers[frame],
                               (void *)state->bps[frame]);
        // Populate the stack of the optimized function with the values the
        // unoptimized function expects
        for (int j = 0; j < unopt_rec.num_locations - 1; j += 2) {
            location_type type = unopt_rec.locations[j].kind;
            uint64_t opt_location_value = direct_locations[loc_index];
            uint64_t loc_size = direct_locations[loc_index + 1];
            if (type == DIRECT) {
                uint64_t unopt_addr = (uint64_t)state->bps[frame] +
                    unopt_rec.locations[j].offset;
                memcpy((void *)unopt_addr, &opt_location_value,
                        loc_size);
                loc_index += 2;
            } else if (type == REGISTER) {
                uint64_t reg_num = unopt_rec.locations[j].dwarf_reg_num;
                memcpy(state->registers[frame] + reg_num,
                       &opt_location_value, loc_size);
                loc_index += 2;
            }
        }
    }
    free(direct_locations);
    free(unopt_call_stk_records);
}

void __guard_failure(int64_t sm_id)
{
    printf("Guard %ld failed!\n", sm_id);

    unw_cursor_t cursor;
    unw_context_t context;
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);
    unw_cursor_t saved_cursor = cursor;

    call_stack_state_t *state = get_call_stack_state(cursor, context);

    void *stack_map_addr = get_addr(".llvm_stackmaps");
    if (!stack_map_addr) {
        errx(1, ".llvm_stackmaps section not found. Exiting.\n");
    }
    stack_map_t *sm = stmap_create(stack_map_addr);
    stack_map_record_t *unopt_rec = stmap_get_map_record(sm, ~sm_id);
    stack_map_record_t *opt_rec = stmap_get_map_record(sm, sm_id);

    if (!unopt_rec || !opt_rec) {
        errx(1, "Stack map record not found. Exiting.\n");
    }
    stack_size_record_t *unopt_size_rec =
        stmap_get_size_record(sm, unopt_rec->index);
    stack_size_record_t *opt_size_rec =
        stmap_get_size_record(sm, opt_rec->index);

    if (!unopt_size_rec || !opt_size_rec) {
        errx(1, "Record not found.");
    }
    // get the end address of the function in which a guard failed
    void *end_addr = get_sym_end((void *)opt_size_rec->fun_addr, "trace");
    uint64_t callback_ret_addr = (uint64_t) __builtin_return_address(0);
    if (callback_ret_addr >= opt_size_rec->fun_addr &&
        callback_ret_addr < (uint64_t)end_addr) {
        printf("A guard failed, but not in an inlined func\n");

    } else {
        printf("A guard failed in an inlined function.\n");
        exit(1);
    }
    restore_unopt_stack(sm, state, unopt_rec, opt_rec);

    // The address to jump to
    addr = unopt_size_rec->fun_addr + unopt_rec->instr_offset;

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
    stmap_free(sm);
    free_call_stack_state(state);
    asm volatile("jmp restore_and_jmp");
}

