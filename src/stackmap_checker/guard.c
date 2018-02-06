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

int get_direct_locations(stack_map_t *sm, void *bp, stack_map_record_t *recs,
        uint32_t rec_count, uint64_t **direct_locs)
{
    // Restore the stack
    int num_locations = 0;
    uint64_t new_size = 0;
    for (int i = 0; i < rec_count; ++i) {
        stack_map_record_t opt_rec = recs[i];
        stack_map_record_t *unopt_rec =
            stmap_get_map_record(sm, ~opt_rec.patchpoint_id);
        new_size += opt_rec.num_locations / 2 * sizeof(uint64_t);
        *direct_locs = (uint64_t *)realloc(*direct_locs, new_size);
        printf("Direct locs %p size %d\n", *direct_locs, new_size);
        // Populate the stack of the optimized function with the values the
        // unoptimized function expects
        for (int i = 0; i < unopt_rec->num_locations - 1; i += 2) {
            location_type type = unopt_rec->locations[i].kind;
            printf("Record %d\n", i);
            uint64_t opt_location_value =
                stmap_get_location_value(sm, opt_rec.locations[i], r, bp);
            if (type == REGISTER) {
                uint16_t reg_num = unopt_rec->locations[i].dwarf_reg_num;
                uint64_t loc_size =
                    stmap_get_location_value(sm, opt_rec.locations[i + 1], r, bp);
                memcpy(r + reg_num, &opt_location_value, loc_size);
            } else if (type == DIRECT) {
                printf("Direct\n");
                uint64_t loc_size =
                    stmap_get_location_value(sm, opt_rec.locations[i + 1], r, bp);
                printf("At index %d at depth %d saving %lu of size %d\n",
                        num_locations, i, opt_location_value,
                        loc_size);
                memcpy(*direct_locs + num_locations, &opt_location_value,
                       loc_size);
                ++num_locations;
            } else if (type == INDIRECT) {
                uint64_t unopt_addr = (uint64_t) bp + unopt_rec->locations[i].offset;
                errx(1, "Not implemented - indirect.\n");
            } else if (type != CONSTANT && type != CONST_INDEX) {
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

    unw_cursor_t cursor;
    unw_context_t context;
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);
    unw_word_t stack_ptr;

    // XXX
    uint64_t **ret_addrs = calloc(MAX_CALL_STACK_DEPTH, sizeof(uint64_t *));
    size_t idx = 0;
    while (unw_step(&cursor) > 0) {
        unw_word_t off, pc;
        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (!pc) {
            break;
        }
        unw_get_reg(&cursor, UNW_REG_SP, &stack_ptr);
        // Find base pointer of each function.
        char fun_name[128];
        unw_get_proc_name(&cursor, fun_name, sizeof(fun_name), &off);
        if (idx > 0) {
            ret_addrs[idx - 1] = (uint64_t *)((char *)stack_ptr - 8);
        }
        idx++;
        if (!strcmp(fun_name, "main")) {
            // --idx; ??
            break;
        }
    }

    void *stack_map_addr = get_addr(".llvm_stackmaps");
    if (!stack_map_addr) {
        errx(1, ".llvm_stackmaps section not found. Exiting.\n");
    }
    stack_map_t *sm = stmap_create(stack_map_addr);
    stmap_print_stack_size_records(sm);

    // Overwrite the old return addresses
    size_t call_stack_depth = idx > 0 ? idx - 1 : 0;

    // Need call_stack_depth + 1 stack maps (1 = the patchpoint which failed);
    // This stores the stack map records of the optimized trace
    stack_map_record_t *call_stk_records = calloc(call_stack_depth + 1,
                                                  sizeof(stack_map_record_t));

    stack_map_record_t *unopt_rec = stmap_get_map_record(sm, ~sm_id);
    stack_map_record_t *opt_rec = stmap_get_map_record(sm, sm_id);

    if (!unopt_rec || !opt_rec) {
        errx(1, "Stack map record not found. Exiting.\n");
    }
    call_stk_records[0] = *opt_rec;

    void *bp = __builtin_frame_address(1);

    stmap_print_map_record(sm, opt_rec->index, r, bp);
    for (int i = 0; i < call_stack_depth; ++i) {
        printf("* Ret addr %p\n", (void *)*(uint64_t *)ret_addrs[i]);
        stack_map_pos_t *sm_pos =
            stmap_get_unopt_return_addr(sm, *(uint64_t *)ret_addrs[i]);
        uint64_t unopt_ret_addr =
            sm->stk_size_records[sm_pos->stk_size_record_index].fun_addr +
            sm->stk_map_records[sm_pos->stk_map_record_index].instr_offset;
        printf("* Using %p instead\n", (void *)unopt_ret_addr);
        *ret_addrs[i] = unopt_ret_addr;
        stack_map_record_t *opt_stk_map_rec =
            stmap_get_map_record(
                sm,
                ~sm->stk_map_records[sm_pos->stk_map_record_index].patchpoint_id);
        call_stk_records[i + 1] =
            sm->stk_map_records[sm_pos->stk_map_record_index];
        stmap_print_map_record(sm, sm_pos->stk_map_record_index, r, bp);
    }

    stack_size_record_t *unopt_size_rec =
        stmap_get_size_record(sm, unopt_rec->index);
    stack_size_record_t *opt_size_rec =
        stmap_get_size_record(sm, opt_rec->index);

    if (!unopt_size_rec || !opt_size_rec) {
        errx(1, "Record not found.");
    }

    uint64_t *direct_locations = NULL;
    int num_locations = get_direct_locations(sm, bp, call_stk_records,
                                             call_stack_depth + 1,
                                             &direct_locations);
    printf("Locations %d\n", num_locations);
    for (int i = 0; i < num_locations; ++i) {
        printf("Location %d %lu\n", i, direct_locations[i]);
    }
    printf("Restoring SM records\n");
    int loc_index = 0;
    // Restore all the stacks on the call stack
    for (int i = 0; i < call_stack_depth + 1; ++i) {
        stack_map_record_t unopt_rec = call_stk_records[i];
        stack_map_record_t *opt_rec =
            stmap_get_map_record(sm, ~unopt_rec.patchpoint_id);
        if (!opt_rec) {
            errx(1, "Optimized stack map record not found.\n");
        }

        printf("Depth %d / %d\n", i, call_stack_depth);
        // Populate the stack of the optimized function with the values the
        // unoptimized function expects
        for (int i = 0; i < unopt_rec.num_locations - 1; i += 2) {
            location_type type = unopt_rec.locations[i].kind;
            printf("Record %d / %d loc index %d\n",
                    i, unopt_rec.num_locations, loc_index);
            uint64_t opt_location_value = direct_locations[loc_index];
            printf("Opt location val %p\n", (void *)opt_location_value);
            if (type == DIRECT) {
                uint64_t unopt_addr = (uint64_t)bp + unopt_rec.locations[i].offset;
                uint64_t loc_size =
                    stmap_get_location_value(sm, opt_rec->locations[i + 1], r, bp);
                printf("Location size %lu\n", loc_size);
                printf("memcpy %lu @ %p\n", opt_location_value, (void*)unopt_addr);
                memcpy((void *)unopt_addr, &opt_location_value,
                        loc_size);
                ++loc_index;
            }
        }
    }
    // The address to jump to
    addr = unopt_size_rec->fun_addr + unopt_rec->instr_offset;

    /*stmap_free(sm);*/
    /*free(direct_locations);*/

    for (int i = 0; i < call_stack_depth; ++i) {
        printf("* Ret addr %p\n", (void *)*(uint64_t *)ret_addrs[i]);
    }
    asm volatile("jmp restore_and_jmp");
}

