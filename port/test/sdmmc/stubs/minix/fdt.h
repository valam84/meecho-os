/* Стенд: слой карты дерева не читает, но sdmmc.h упоминает struct fdt_node. */
#ifndef _STUB_MINIX_FDT_H
#define _STUB_MINIX_FDT_H
#include <minix/type.h>
struct fdt_node { const void *dtb; const void *props; unsigned addr_cells, size_cells; };
int fdt_node_is_compatible(const struct fdt_node *node, const char *want);
#endif
