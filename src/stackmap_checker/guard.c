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

// the label to jump to whenever a guard fails
extern void restore_and_jmp(void);
uint64_t addr = 0;
uint64_t r[16];

int get_direct_locations(stack_map_t *sm, uint64_t *bps, stack_map_record_t *recs,
        uint32_t rec_count, uint64_t **direct_locs, unw_word_t **registers)
{
    // Restore the stack
    int num_locations = 0;
    uint64_t new_size = 0;
    // Each record corresponds to a stack frame.
    for (int i = 0; i < rec_count; ++i) {
        stack_map_record_t opt_rec = recs[i];
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
                                         registers[i], (void *)bps[i]);

            uint64_t loc_size =
                stmap_get_location_value(sm, opt_rec.locations[j + 1],
                                         registers[i], (void *)bps[i]);
            if (type == DIRECT) {
                printf("Direct\n");
                printf("At index %d at depth %d saving %lu of size %d\n",
                        num_locations, i, opt_location_value,
                        loc_size);
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
                uint64_t unopt_addr = (uint64_t) bps[i] +
                    unopt_rec->locations[j].offset;
                errx(1, "Not implemented - indirect.\n");
            } else if (type != CONSTANT && type != CONST_INDEX && type != REGISTER) {
                errx(1, "Unknown record - %u. Exiting\n", type);
            }
        }
    }
    return num_locations;
}

void __guard_failure(int64_t sm_id)
{
    // save register the state in global array r
    asm volatile("mov %%rax,%0\n"
                 "mov %%rcx,%1\n"
                 "mov %%rdx,%2\n"
                 "mov %%rbx,%3\n"
                 "mov %%rsp,%4\n"
                 "mov %%rbp,%5\n"
                 "mov %%rsi,%6\n"
                 "mov %%rdi,%7\n" : "=m"(r[0]), "=m"(r[1]), "=m"(r[2]),
                                    "=m"(r[3]), "=m"(r[4]), "=m"(r[5]),
                                    "=m"(r[6]), "=m"(r[7]) : : );
    asm volatile("mov %%r8,%0\n"
                 "mov %%r9,%1\n"
                 "mov %%r10,%2\n"
                 "mov %%r11,%3\n"
                 "mov %%r12,%4\n"
                 "mov %%r13,%5\n"
                 "mov %%r14,%6\n"
                 "mov %%r15,%7\n" : "=m"(r[8]), "=m"(r[9]), "=m"(r[10]),
                                    "=m"(r[11]), "=m"(r[12]), "=m"(r[13]),
                                    "=m"(r[14]), "=m"(r[15]) : : );
    printf("Guard %ld failed!\n", sm_id);

    unw_word_t **registers = calloc(MAX_CALL_STACK_DEPTH, sizeof(unw_word_t *));

    unw_cursor_t cursor;
    unw_context_t context;
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    unw_cursor_t saved_cursor = cursor;
    unw_word_t stack_ptr;
    unw_word_t base_ptr;
    // XXX
    uint64_t **ret_addrs = calloc(MAX_CALL_STACK_DEPTH, sizeof(uint64_t *));
    uint64_t *bps = calloc(MAX_CALL_STACK_DEPTH, sizeof(uint64_t));
    size_t frame = 0;
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
        unw_get_reg(&cursor, UNW_REG_SP,     &stack_ptr);


        // Find base pointer of each function.
        char fun_name[128];
        unw_get_proc_name(&cursor, fun_name, sizeof(fun_name), &off);
        ret_addrs[frame] =
            (uint64_t *)((char *)registers[frame][6] + 8);
        if (unw_get_reg(&cursor, UNW_TDEP_BP, &base_ptr)) {
            printf("bp not found\n");
        }
        *(bps + frame) = base_ptr;
        frame++;
        if (!strcmp(fun_name, "main")) {
            --frame;
            break;
        }
    }

    void *stack_map_addr = get_addr(".llvm_stackmaps");
    if (!stack_map_addr) {
        errx(1, ".llvm_stackmaps section not found. Exiting.\n");
    }
    stack_map_t *sm = stmap_create(stack_map_addr);

    // Overwrite the old return addresses
    size_t call_stack_depth = frame > 0 ? frame - 1 : 0;

    // Need call_stack_depth + 1 stack maps (1 = the patchpoint which failed);
    // This stores the stack map records of the unoptimized trace
    stack_map_record_t *unopt_call_stk_records = calloc(call_stack_depth + 1,
                                                  sizeof(stack_map_record_t));

    stack_map_record_t *opt_call_stk_records = calloc(call_stack_depth + 1,
                                                  sizeof(stack_map_record_t));
    stack_map_record_t *unopt_rec = stmap_get_map_record(sm, ~sm_id);
    stack_map_record_t *opt_rec = stmap_get_map_record(sm, sm_id);

    if (!unopt_rec || !opt_rec) {
        errx(1, "Stack map record not found. Exiting.\n");
    }

    // Overwrite the old return addresses and store the stack map records
    // that correspond to each call which generated this stack trace in
    // unopt_call_stk_records.
    unopt_call_stk_records[0] = *unopt_rec;
    opt_call_stk_records[0] = *opt_rec;
    for (int i = 0; i < call_stack_depth; ++i) {
        stack_map_pos_t *sm_pos =
            stmap_get_unopt_return_addr(sm, *(uint64_t *)ret_addrs[i]);
        uint64_t unopt_ret_addr =
            sm->stk_size_records[sm_pos->stk_size_record_index].fun_addr +
            sm->stk_map_records[sm_pos->stk_map_record_index].instr_offset;
        *ret_addrs[i] = unopt_ret_addr;
        stack_map_record_t *opt_stk_map_rec =
            stmap_get_map_record(
                sm,
                ~sm->stk_map_records[sm_pos->stk_map_record_index].patchpoint_id);
        unopt_call_stk_records[i + 1] =
            sm->stk_map_records[sm_pos->stk_map_record_index];
        opt_call_stk_records[i + 1] = *opt_stk_map_rec;
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
    }
    uint64_t *direct_locations = NULL;
    int num_locations = get_direct_locations(sm, bps, opt_call_stk_records,
                                             call_stack_depth + 1,
                                             &direct_locations,
                                             registers);

    int loc_index = 0;

    for (int frame = 0; frame < call_stack_depth; ++frame) {
        printf("BP %p\n", bps[frame]);
    }
    // Restore all the stacks on the call stack
    for (int frame = 0; frame < call_stack_depth + 1; ++frame) {
        stack_map_record_t unopt_rec = unopt_call_stk_records[frame];

        stmap_print_map_record(sm, unopt_rec.index,
                               registers[frame], (void *)bps[frame]);
        // Populate the stack of the optimized function with the values the
        // unoptimized function expects
        for (int j = 0; j < unopt_rec.num_locations - 1; j += 2) {
            location_type type = unopt_rec.locations[j].kind;
            uint64_t opt_location_value = direct_locations[loc_index];
            uint64_t loc_size = direct_locations[loc_index + 1];
            if (type == DIRECT) {
                uint64_t unopt_addr = (uint64_t)bps[frame] +
                    unopt_rec.locations[j].offset;
                memcpy((void *)unopt_addr, &opt_location_value,
                        loc_size);
                loc_index += 2;
            } else if (type == REGISTER) {
                uint64_t reg_num = unopt_rec.locations[j].dwarf_reg_num;
                memcpy(registers[frame] + reg_num, &opt_location_value, loc_size);
                loc_index += 2;
            }
        }
    }
    // The address to jump to
    addr = unopt_size_rec->fun_addr + unopt_rec->instr_offset;
    frame = 0;
    while (unw_step(&saved_cursor) > 0) {
        unw_word_t off, pc;
        if (!pc) {
            break;
        }
        for (int i = 0; i < 16; ++i) {
            if (i != UNW_X86_64_RSP && i != UNW_X86_64_RBP) {
                unw_set_reg(&cursor, i, registers[frame][i]);
            }
        }
        ++frame;
    }
    stmap_free(sm);
    free(direct_locations);
    free(ret_addrs);
    free(bps);
    asm volatile("jmp restore_and_jmp");
}

