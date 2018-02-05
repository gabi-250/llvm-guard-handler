#include <elf.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "utils.h"

#define TRACE_BIN "trace"

Elf64_Ehdr* get_elf_header(const char *section_name)
{
    int fd = open(TRACE_BIN, O_RDONLY);
    void *data = mmap(NULL,
                      lseek(fd, 0, SEEK_END), // file size
                      PROT_READ,
                      MAP_SHARED, fd, 0);
    close(fd);
    Elf64_Ehdr *elf = (Elf64_Ehdr *) data;
    Elf64_Shdr *shdr = (Elf64_Shdr *) ((char *)data + elf->e_shoff);
    char *strtab = (char *)data + shdr[elf->e_shstrndx].sh_offset;
    return elf;
}

void* get_addr(const char *section_name)
{
    Elf64_Ehdr *elf = get_elf_header(section_name);
    Elf64_Shdr *shdr = (Elf64_Shdr *) ((char *)elf + elf->e_shoff);
    char *strtab = (char *)elf + shdr[elf->e_shstrndx].sh_offset;
    for(int i = 0; i < elf->e_shnum; i++) {
        if (strcmp(section_name, &strtab[shdr[i].sh_name]) == 0) {
            return (void *)shdr[i].sh_addr;
        }
    }
    return NULL;
}

void* get_sym_end(void *start_addr, const char *section_name)
{
    Elf64_Ehdr *elf = get_elf_header(section_name);
    Elf64_Shdr *shdr = (Elf64_Shdr *) ((char *)elf + elf->e_shoff);
    char *strtab = (char *)elf + shdr[elf->e_shstrndx].sh_offset;
    for(int i = 0; i < elf->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            Elf64_Sym *stab = (Elf64_Sym *)((char *)elf + shdr[i].sh_offset);
            int symbol_count = shdr[i].sh_size / sizeof(Elf64_Sym);
            for (int i = 0; i < symbol_count; ++i) {
                if ((void *)stab[i].st_value == start_addr) {
                    return (char *)start_addr + stab[i].st_size;
                }
            }
        }
    }
    return NULL;
}
