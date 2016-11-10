#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

int parse_maps(char *libname, char **fullpath, void **base);
int symbol_library_info(const char *symbol, const char **fname, void **fbase);
int get_module_base_address(char const *module_filename, void *handle, void **base);
void *elf_hook(char const *module_filename, void const *module_address, char const *name, void const *substitution);

#ifdef __cplusplus
}
#endif
