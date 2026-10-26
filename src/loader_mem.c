#include <stdio.h>
#include <string.h>
#include "include/loader.h"
#include "include/bus.h"

/* 从内存里的 ELF 镜像加载（供 WASM 前端用） */
typedef struct {
    u8  e_ident[16];
    u16 e_type, e_machine;
    u32 e_version, e_entry, e_phoff, e_shoff, e_flags;
    u16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} ehdr_t;

typedef struct {
    u32 p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} phdr_t;

typedef struct {
    u32 sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size;
    u32 sh_link, sh_info, sh_addralign, sh_entsize;
} shdr_t;

typedef struct {
    u32 st_name, st_value, st_size;
    u8  st_info, st_other;
    u16 st_shndx;
} sym_t;

bool loader_load_elf_mem(const u8 *buf, long sz, loaded_t *out)
{
    memset(out, 0, sizeof(*out));
    if (sz < (long)sizeof(ehdr_t)) return false;
    ehdr_t *eh = (ehdr_t *)buf;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[4] != 1) return false;

    out->entry = eh->e_entry;
    for (u32 i = 0; i < eh->e_phnum; i++) {
        phdr_t *ph = (phdr_t *)(buf + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != 1) continue;
        if (ph->p_offset + ph->p_filesz > (u32)sz) return false;
        if (ph->p_paddr < RAM_BASE ||
            (u64)ph->p_paddr + ph->p_memsz > (u64)RAM_BASE + RAM_SIZE) return false;
        u32 off = ph->p_paddr - RAM_BASE;
        memcpy(bus_ram() + off, buf + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            memset(bus_ram() + off + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
        out->loaded_bytes += ph->p_memsz;
    }

    if (eh->e_shoff && eh->e_shnum) {
        shdr_t *shs = (shdr_t *)(buf + eh->e_shoff);
        for (u32 i = 0; i < eh->e_shnum; i++) {
            shdr_t *sh = &shs[i];
            if (sh->sh_type != 2 || sh->sh_entsize == 0) continue;
            shdr_t *strsh = &shs[sh->sh_link];
            const char *strtab = (const char *)(buf + strsh->sh_offset);
            u32 n = sh->sh_size / sh->sh_entsize;
            for (u32 k = 0; k < n; k++) {
                sym_t *sym = (sym_t *)(buf + sh->sh_offset + k * sh->sh_entsize);
                const char *nm = strtab + sym->st_name;
                if (!strcmp(nm, "tohost")) { out->tohost_addr = sym->st_value; out->has_tohost = true; }
                else if (!strcmp(nm, "fromhost")) { out->fromhost_addr = sym->st_value; out->has_fromhost = true; }
            }
            break;
        }
    }
    return true;
}