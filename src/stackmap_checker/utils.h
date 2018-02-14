#ifndef UTILS_H
#define UTILS_H

/*
 * Return the start address of the specified section.
 */
void *get_addr(const char *section_name);

/*
 * Return the end address of the symbol with the specified start address.
 */
void* get_sym_end(void *start_addr, const char *section_name);
#endif // UTILS_H
