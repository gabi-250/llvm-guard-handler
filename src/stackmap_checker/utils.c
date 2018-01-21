#include <elf.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "utils.h"

void *get_addr(const char *section_name)
{
    int fd = open("trace", O_RDONLY);
    void *data = mmap(NULL,
                      lseek(fd, 0, SEEK_END), // file size
                      PROT_READ,
                      MAP_SHARED, fd, 0);
    close(fd);
    Elf64_Ehdr *elf = (Elf64_Ehdr *) data;
    Elf64_Shdr *shdr = (Elf64_Shdr *) ((char *)data + elf->e_shoff);
    char *strtab = (char *)data + shdr[elf->e_shstrndx].sh_offset;
    for(int i = 0; i < elf->e_shnum; i++) {
        if (strcmp(section_name, &strtab[shdr[i].sh_name]) == 0) {
            return (char *)shdr[i].sh_offset + 0x400000;
        }
    }
    return NULL;
}

