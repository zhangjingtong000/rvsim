#ifndef RV_LOADER_H
#define RV_LOADER_H

#include "rv_types.h"

typedef struct {
    u32  entry;
    u32  tohost_addr;
    u32  fromhost_addr;
    u32  begin_signature;
    u32  end_signature;
    bool has_tohost;
    bool has_fromhost;
    u32  loaded_bytes;
} loaded_t;

/* 加载 ELF32（静态链接，无重定位） */
bool loader_load_elf(const char *path, loaded_t *out);
/* 从内存加载（供 WASM 前端） */
bool loader_load_elf_mem(const u8 *buf, long sz, loaded_t *out);
/* 加载裸 Image 到指定物理地址 */
bool loader_load_raw(const char *path, u32 addr, u32 *entry_out, u32 *size_out);

#endif