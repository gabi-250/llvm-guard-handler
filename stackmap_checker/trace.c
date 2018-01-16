#include <stdio.h>
#include <stdint.h>
#include <elf.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include "stmap.h"

void *get_addr(char *section_name)
{
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

void __guard_failure(int64_t sm_id)
{
    uint64_t r[16];
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

    uint64_t addr = 0;
    uint64_t target_sm_id = 0; // XXX
    stack_map_record_t smap_rec = stack_map->stk_map_records[target_sm_id];
    stack_size_record_t ssize_rec = stack_map->stk_size_records[target_sm_id];
    addr = ssize_rec.fun_addr + smap_rec.instr_offset;

    free_stack_map(stack_map);
    uint64_t stack_size = 0;
    uint64_t aux = 0;
    asm volatile("mov %%rsp,%1\n"
                 "mov %%rbp,%0\n"
                 "sub %1,%0" : "=r"(stack_size), "=r"(aux) : : );
    asm volatile("add %1,%%rsp\n"           // return to the stack of 'trace'
                 "pop %%rbp\n"
                                            // make sure the format string is in rdi
                 "subl $0xA,-0x10(%%rbp)\n" // find the format string used in unopt
                 "movl $0x5,-0x4(%%rbp)\n"  // print 5 instead of 2
                 "jmp *%0": : "r"(addr), "m"(stack_size) : "%rsp", "%rdi", "%rax");
}

void unopt()
{
    int x = 2;
    printf("unopt %d\n", x);
    exit(x);
}

void trace()
{
    int x = 5;
    printf("trace %d\n", x);
}

int main(int argc, char **argv)
{
    trace();
    return 0;
}
