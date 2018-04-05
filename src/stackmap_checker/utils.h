#ifndef UTILS_H
#define UTILS_H

/*
 * Return the start address of the specified section.
 */
void* get_addr(const char *bin_name, const char *section_name);

/*
 * Return the end address of the symbol with the specified start address.
 */
uint64_t get_sym_end(uint64_t start_addr);

/*
 * Return the start address of the function which contains `addr`.
 */
uint64_t get_sym_start(uint64_t addr);

/*
 * Return the absolute path of this executable.
 */
char* get_binary_path();

#endif // UTILS_H
