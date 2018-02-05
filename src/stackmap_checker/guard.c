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
    uint64_t **ret_addrs = malloc(MAX_CALL_STACK_DEPTH * sizeof(uint64_t *));
    size_t idx = 0;
    while (unw_step(&cursor) > 0) {
        unw_word_t off, pc;
        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (!pc) {
            break;
        }
        unw_get_reg(&cursor, UNW_REG_SP, &stack_ptr);

        // 1. find base pointer of each function
        // 2. replace return addresses with addresses from unopt
        // 3. Restore all the stacks in the call stack
        char fun_name[128];
        int ret = !unw_get_proc_name(&cursor, fun_name, sizeof(fun_name), &off);

        if (idx > 0) {
            ret_addrs[idx - 1] = (uint64_t *)((char *)stack_ptr - 8);
        }
        idx++;

        if (!strcmp(fun_name, "main")) {
            break;
        }
    }
    size_t call_stack_depth = idx > 0 ? idx - 1 : 0;
    void *stack_map_addr = get_addr(".llvm_stackmaps");
    if (!stack_map_addr) {
        errx(1, ".llvm_stackmaps section not found. Exiting.\n");
    }

    stack_map_t *sm = stmap_create(stack_map_addr);
    for (int i = 0; i < call_stack_depth; ++i) {
        printf("* Ret addr %p\n", *(uint64_t *)ret_addrs[i]);
        uint64_t unopt_ret_addr =
            stmap_get_unopt_return_addr(sm, *ret_addrs[i]);
        printf("Using %p instead\n", unopt_ret_addr);
        *ret_addrs[i] = unopt_ret_addr;
    }
    return ;
    void *bp = __builtin_frame_address(1);

    int unopt_rec_idx = stmap_get_map_record(sm, ~sm_id);
    int opt_rec_idx = stmap_get_map_record(sm, sm_id);

    if (unopt_rec_idx == -1 || opt_rec_idx == -1) {
        errx(1, "Stack map record not found. Exiting.\n");
    }


    stack_map_record_t unopt_rec = sm->stk_map_records[unopt_rec_idx];
    stack_map_record_t opt_rec = sm->stk_map_records[opt_rec_idx];

    stack_size_record_t unopt_size_rec =
        sm->stk_size_records[stmap_get_size_record(sm, unopt_rec_idx)];

    stack_size_record_t opt_size_rec =
        sm->stk_size_records[stmap_get_size_record(sm, opt_rec_idx)];

    // Restore the stack
    uint64_t *direct_locations =
        (uint64_t *)malloc(sizeof(uint64_t) * unopt_rec.num_locations);
    // Populate the stack of the optimized function with the values the
    // unoptimized function expects
    for (int i = 1; i < unopt_rec.num_locations - 1; i += 2) {
        location_type type = unopt_rec.locations[i].kind;
        uint64_t opt_location_value =
            stmap_get_location_value(sm, opt_rec.locations[i], r, bp);
        if (type == REGISTER) {
            uint16_t reg_num = unopt_rec.locations[i].dwarf_reg_num;
            uint64_t loc_size =
                stmap_get_location_value(sm, opt_rec.locations[i + 1], r, bp);
            memcpy(r + reg_num, &opt_location_value, loc_size);
        } else if (type == DIRECT) {
            uint64_t loc_size =
                stmap_get_location_value(sm, opt_rec.locations[i + 1], r, bp);
            memcpy(direct_locations + i, &opt_location_value,
                    loc_size);
        } else if (type == INDIRECT) {
            uint64_t unopt_addr = (uint64_t) bp + unopt_rec.locations[i].offset;
            errx(1, "Not implemented - indirect.\n");
        } else if (type != CONSTANT && type != CONST_INDEX) {
            errx(1, "Unknown record - %u. Exiting\n", type);
        }
    }

    for (int i = 1; i < unopt_rec.num_locations - 1; i += 2) {
        location_type type = unopt_rec.locations[i].kind;
        if (type == DIRECT) {
            uint64_t unopt_addr = (uint64_t)bp + unopt_rec.locations[i].offset;
            uint64_t loc_size =
                stmap_get_location_value(sm, opt_rec.locations[i + 1], r, bp);
            memcpy((void *)unopt_addr, direct_locations + i, loc_size);
        }
    }
    addr = unopt_size_rec.fun_addr + unopt_rec.instr_offset;
    stmap_free(sm);
    free(direct_locations);
    asm volatile("jmp restore_and_jmp");
}

