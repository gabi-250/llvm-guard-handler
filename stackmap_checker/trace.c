#include <stdio.h>
#include <stdint.h>
#include <elf.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include "stmap.h"

void *get_addr(char *section_name) {
    int fd = open("trace", O_RDONLY);
    void *data = mmap(NULL,
                      lseek(fd, 0, SEEK_END), // file size
                      PROT_READ,
                      MAP_SHARED, fd, 0);
    close(fd);
    Elf64_Ehdr *elf = (Elf64_Ehdr *) data;
    Elf64_Shdr *shdr = (Elf64_Shdr *) (data + elf->e_shoff);
    char *strtab = (char *)(data + shdr[elf->e_shstrndx].sh_offset);
    for(int i = 0; i < elf->e_shnum; i++) {
        if (strcmp(section_name, &strtab[shdr[i].sh_name]) == 0) {
            return (void *) shdr[i].sh_offset + 0x400000;
        }
    }
    return NULL;
}

void unopt();

void __guard_failure(int64_t sm_id) {
    uint64_t r[16];
    asm volatile("mov %%rax,%0\n"
                 "mov %%rcx,%1\n"
                 "mov %%rdx,%2\n"
                 "mov %%rbx,%3\n"
                 "mov %%rsp,%4\n"
                 "mov %%rbp,%5\n"
                 "mov %%rsi,%6\n"
                 "mov %%rdi,%7\n" : "=r"(r[0]), "=r"(r[1]), "=r"(r[2]),
                                    "=r"(r[3]), "=r"(r[4]), "=r"(r[5]),
                                    "=r"(r[6]), "=r"(r[7]) : : );
    asm volatile("mov %%r8,%0\n"
                 "mov %%r9,%1\n"
                 "mov %%r10,%2\n"
                 "mov %%r11,%3\n"
                 "mov %%r12,%4\n"
                 "mov %%r13,%5\n"
                 "mov %%r14,%6\n"
                 "mov %%r15,%7\n" : "=r"(r[8]), "=r"(r[9]), "=r"(r[10]),
                                    "=r"(r[11]), "=r"(r[12]), "=r"(r[13]),
                                    "=r"(r[14]), "=r"(r[15]) : : );
    printf("Guard %ld failed!\n", sm_id);
    void *stack_map_addr = get_addr(".llvm_stackmaps");
    if (!stack_map_addr) {
        printf(".llvm_stackmaps section not found. Exiting.\n");
        exit(1);
    }
    stack_map_t *stack_map = create_stack_map(stack_map_addr);
    void *bp = __builtin_frame_address(1);
    print_locations(stack_map, bp, r);
    print_liveouts(stack_map, r);
    print_constants(stack_map);

    uint64_t fun_addr = 0;
    for (size_t j = 0; j < stack_map->num_rec; ++j) {
        stack_size_record_t s_rec = stack_map->stk_size_records[j];
        if ((void *)s_rec.fun_addr == (void *) unopt) {
            // useless check, need to work out where to jump
            fun_addr = s_rec.fun_addr;
        }
    }
    free_stack_map(stack_map);

    asm volatile("mov %0,%%rax\n"
                 "mov %1,%%rcx\n"
                 "mov %2,%%rdx\n"
                 "mov %3,%%rbx\n"
                 "mov %4,%%rsp\n"
                 "mov %5,%%rbp\n"
                 "mov %6,%%rsi\n"
                 "mov %7,%%rdi\n" : : "r"(r[0]), "r"(r[1]), "r"(r[2]),
                                      "r"(r[3]), "r"(r[4]), "r"(r[5]),
                                      "r"(r[6]), "r"(r[7]) : );
    asm volatile("mov %0,%%r8\n"
                 "mov %1,%%r9\n"
                 "mov %2,%%r10\n"
                 "mov %3,%%r11\n"
                 "mov %4,%%r12\n"
                 "mov %5,%%r13\n"
                 "mov %6,%%r14\n"
                 "mov %7,%%r15\n" : : "r"(r[8]), "r"(r[9]), "r"(r[10]),
                                      "r"(r[11]), "r"(r[12]), "r"(r[13]),
                                      "r"(r[14]), "r"(r[15]) : );
    asm volatile("movl $0x5,-0x4(%rbp)");
    asm volatile("jmp *%0" : : "r"(fun_addr + 15));
}

void unopt() {
    int x = 2;
    putc(x + '0', stdout);
    putc('\n', stdout);
    exit(x);
}

void trace() {
    int x = 5;
    printf("trace %d @ %p\n", x, (void *)&x);
}

int main(int argc, char **argv) {
    trace();
    return 0;
}
