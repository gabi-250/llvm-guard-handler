#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include "stmap.h"
#include "utils.h"

// the label to jump to whenever a guard fails
extern void restore_and_jmp(void);

// The address to jump to. This represents the address execution should resume
// at.
uint64_t addr = 0;

// The saved registers.
uint64_t r[16];
uint64_t fun_ret_addr = 0;

int get_number(void);
void trace(void);

/*
 * The guard failure handler.
 *
 * This is supposed to restore the stack/register state and resume execution in
 * the unoptimized version of the trace.
 */
void __guard_failure(int64_t sm_id)
{
    // Save register the state in global array r
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
    void *stack_map_addr = get_addr(".llvm_stackmaps");
    if (!stack_map_addr) {
        errx(1, ".llvm_stackmaps section not found. Exiting.\n");
    }

    stack_map_t *sm = stmap_create(stack_map_addr);

    // The frame address of `trace`.
    void *bp = __builtin_frame_address(1);

    // Retrieve the indices of the two stack map records which correspond to
    // the point where a guard failed.
    // unopt_rec_idx is the index of the stack map record of the unoptimized
    // version of the function in which a guard failed
    int unopt_rec_idx = stmap_get_map_record(sm, ~sm_id);
    // The index of the record which corresponds to the patchpoint which
    // called __guard_failure.
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

    // get the end address of the function in which a guard failed
    void *end_addr = get_sym_end((void *)opt_size_rec.fun_addr, "trace");
    uint64_t callback_ret_addr = (uint64_t) __builtin_return_address(0);
    if (callback_ret_addr >= opt_size_rec.fun_addr &&
        callback_ret_addr < (uint64_t)end_addr) {
        printf("A guard failed, but not in an inlined func\n");
    } else {
        printf("A guard failed in an inlined function.\n");
    }

    // Save the direct (stack) locations. These need to be restored later,
    // because the process of restoring them might cause other values on the
    // stack of `trace` to be overwritten.
    uint64_t *direct_locations =
        (uint64_t *)calloc(unopt_rec.num_locations, sizeof(uint64_t));
    // Locations are considered in pairs,  which is why the loop counter is
    // incremented by 2. This is because locations stored at odd indices
    // represent the sizes of the other locations. This is important because
    // the runtime needs to know how many bytes to copy from each location to
    // restore the stack state.
    for (int i = 1; i < unopt_rec.num_locations - 1; i += 2) {
        location_type type = unopt_rec.locations[i].kind;
        // Get the value of the current location. This value needs to be placed
        // on the stack of the unoptimized function, or in a register.
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

    // Populate the stack of the optimized function with the values the
    // unoptimized function expects.
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

int get_number()
{
    return 3;
}

void trace()
{
    int x = get_number();
    putchar(x +'0');
    putchar('\n');
    exit(x);
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
