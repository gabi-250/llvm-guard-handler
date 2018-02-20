#include <elf.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <err.h>
#include <stdlib.h>
#include "utils.h"

#define MAX_BUF_SIZE 512

char* get_binary_path()
{
    char *buff = malloc(MAX_BUF_SIZE);
    int buff_size = readlink("/proc/self/exe", buff, MAX_BUF_SIZE - 1);
    if (buff_size == -1) {
        errx(1, "Could not find the path of the executable.\n");
    }
    buff[buff_size] = '\0';
    return buff;
}

Elf64_Ehdr* get_elf_header(const char *bin_name)
{
    int fd = open(bin_name, O_RDONLY);
    void *data = mmap(NULL,
                      lseek(fd, 0, SEEK_END), // file size
                      PROT_READ,
                      MAP_SHARED, fd, 0);
    close(fd);
    Elf64_Ehdr *elf = (Elf64_Ehdr *) data;
    Elf64_Shdr *shdr = (Elf64_Shdr *) ((char *)data + elf->e_shoff);
    return elf;
}

void free_header(const char *bin_name, Elf64_Ehdr *elf)
{
    int fd = open(bin_name, O_RDONLY);
    munmap(elf, lseek(fd, 0, SEEK_END)); // file size
    close(fd);
}

void* get_addr(const char *bin_name, const char *section_name)
{
    Elf64_Ehdr *elf = get_elf_header(bin_name);
    Elf64_Shdr *shdr = (Elf64_Shdr *) ((char *)elf + elf->e_shoff);
    char *strtab = (char *)elf + shdr[elf->e_shstrndx].sh_offset;
    for(int i = 0; i < elf->e_shnum; i++) {
        if (strcmp(section_name, &strtab[shdr[i].sh_name]) == 0) {
            void *addr = (void *)shdr[i].sh_addr;
            free_header(bin_name, elf);
            return addr;
        }
    }
    free_header(bin_name, elf);
    return NULL;
}

void* get_sym_end(const char *bin_name, void *start_addr)
{
    Elf64_Ehdr *elf = get_elf_header(bin_name);
    Elf64_Shdr *shdr = (Elf64_Shdr *) ((char *)elf + elf->e_shoff);
    char *strtab = (char *)elf + shdr[elf->e_shstrndx].sh_offset;
    for(int i = 0; i < elf->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            Elf64_Sym *stab = (Elf64_Sym *)((char *)elf + shdr[i].sh_offset);
            int symbol_count = shdr[i].sh_size / sizeof(Elf64_Sym);
            for (int i = 0; i < symbol_count; ++i) {
                if ((void *)stab[i].st_value == start_addr) {
                    void *addr = (char *)start_addr + stab[i].st_size;
                    free_header(bin_name, elf);
                    return addr;
                }
            }
        }
    }
    free_header(bin_name, elf);
    return NULL;
}
