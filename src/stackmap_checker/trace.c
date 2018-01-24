#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "stmap.h"
#include "utils.h"

extern void restore_and_jmp(void);
uint64_t addr = 0;
uint64_t stack_size = 0;
uint64_t r[16];

int get_number(void);

void __guard_failure(int64_t sm_id)
{
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
        printf(".llvm_stackmaps section not found. Exiting.\n");
        exit(1);
    }
    stack_map_t *stack_map = create_stack_map(stack_map_addr);
    void *bp = __builtin_frame_address(1);

    int unopt_stk_map_rec_idx = get_record(stack_map, ~sm_id);
    int opt_stk_map_rec_idx = get_record(stack_map, sm_id);

    if (unopt_stk_map_rec_idx == -1 || opt_stk_map_rec_idx == -1) {
        exit(1);
    }
    stack_map_record_t unopt_rec =
        stack_map->stk_map_records[unopt_stk_map_rec_idx];
    stack_map_record_t opt_rec =
        stack_map->stk_map_records[opt_stk_map_rec_idx];
    stack_size_record_t ssize_rec =
        *get_stack_size_record(stack_map, unopt_stk_map_rec_idx);
    for (size_t i = 0; i < unopt_rec.num_locations; ++i) {
        location_type type = unopt_rec.locations[i].kind;
        if (type == REGISTER) {
            uint16_t reg_num = unopt_rec.locations[i].dwarf_reg_num;
            if (opt_rec.locations[i].kind == DIRECT) {
                uint64_t addr = (uint64_t)bp + opt_rec.locations[i].offset;
                r[reg_num] = addr;
            } else if (opt_rec.locations[i].kind == CONSTANT) {
                r[reg_num] = opt_rec.locations[i].offset;
            } else if (opt_rec.locations[i].kind == REGISTER) {
                uint16_t opt_reg_num = opt_rec.locations[i].dwarf_reg_num;
                r[reg_num] = r[opt_reg_num];
            } else {
                printf("Not implemented - register\n");
                exit(1);
            }
        } else if (type == DIRECT) {
            uint64_t unopt_addr = (uint64_t)bp + unopt_rec.locations[i].offset;
            if (opt_rec.locations[i].kind == DIRECT) {
                uint64_t addr = (uint64_t)bp + opt_rec.locations[i].offset;
                *(uint64_t *)unopt_addr = *(uint64_t *)addr;
            } else if (opt_rec.locations[i].kind == REGISTER) {
                uint16_t reg_num = opt_rec.locations[i].dwarf_reg_num;
                *(uint64_t *)unopt_addr = r[reg_num];
            } else {
                printf("Not implemented - direct\n");
                exit(1);
            }
        } else if (type == INDIRECT) {
            // XXX
            uint64_t addr = r[unopt_rec.locations[i].dwarf_reg_num] +
                unopt_rec.locations[i].offset;
            printf("Not implemented - indirect\n");
            exit(1);
        } else if (type == CONST_INDEX) {
            int32_t offset = unopt_rec.locations[i].offset;
            printf("Not implemented - const index\n");
            exit(1);
        } else if (type != CONSTANT) {
            printf("Not implemented - unknown\n");
            exit(1);
        }
    }
    addr = ssize_rec.fun_addr + unopt_rec.instr_offset;
    uint64_t aux = 0;
    stack_size_record_t opt_ssize_rec =
        *get_stack_size_record(stack_map, opt_stk_map_rec_idx);
    uint64_t new_stack_size = ssize_rec.stack_size;
    free_stack_map(stack_map);
    asm volatile("mov %%rsp,%1\n"
                 "mov %%rbp,%0\n"
                 "sub %1,%0" : "=r"(stack_size), "=r"(aux) : : );
    asm volatile("jmp restore_and_jmp");
}

int get_number() {
    return 3;
}

void trace()
{
    int y = get_number();
    int x = 2;
    putchar(x + '0');
    putchar('\n');
    putchar(y + '0');
    putchar('\n');
    if (get_number() == 3) {
        putchar(get_number() + '0');
        putchar('\n');
    }
    exit(x);
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
