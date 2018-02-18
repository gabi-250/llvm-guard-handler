#ifndef UTILS_H
#define UTILS_H

/*
 * Return the start address of the specified section.
 */
void* get_addr(const char *bin_name, const char *section_name);

/*
 * Return the end address of the symbol with the specified start address.
 */
void* get_sym_end(const char *bin_name, void *start_addr);

char* get_binary_path();

#endif // UTILS_H
