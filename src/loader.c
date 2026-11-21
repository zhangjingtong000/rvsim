#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/loader.h"
#include "include/bus.h"

/* 最小 ELF32 结构 */
typedef struct {
    u8  e_ident[16];
    u16 e_type, e_machine;
    u32 e_version, e_entry, e_phoff, e_shoff, e_flags;
    u16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} elf32_ehdr_t;

typedef struct {
    u32 p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} elf32_phdr_t;

typedef struct {
    u32 sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size;
    u32 sh_link, sh_info, sh_addralign, sh_entsize;
} elf32_shdr_t;

typedef struct {
    u32 st_name, st_value, st_size;
    u8  st_info, st_other;
    u16 st_shndx;
} elf32_sym_t;

#define PT_LOAD     1
#define SHT_SYMTAB  2
#define SHT_STRTAB  3

static u8 *read_file(const char *path, long *sz)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *sz = ftell(f);
    rewind(f);
    u8 *buf = malloc((size_t)*sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)*sz, f) != (size_t)*sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    return buf;
}

bool loader_load_elf(const char *path, loaded_t *out)
{
    memset(out, 0, sizeof(*out));
    long sz = 0;
    u8 *buf = read_file(path, &sz);
    if (!buf) { fprintf(stderr, "loader: 打不开 %s\n", path); return false; }

    if (sz < (long)sizeof(elf32_ehdr_t)) { free(buf); return false; }
    elf32_ehdr_t *eh = (elf32_ehdr_t *)buf;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') {
        fprintf(stderr, "loader: 不是 ELF\n"); free(buf); return false;
    }
    if (eh->e_ident[4] != 1) { fprintf(stderr, "loader: 不是 ELF32\n"); free(buf); return false; }

    /* 文件头里的偏移和数量全部来自不可信文件，做指针算术前先逐个验界。
       原来的写法直接 buf + e_phoff + i * e_phentsize，一个
       e_phoff=0xFFFFFFF0 的 ELF 就能让模拟器读越界。
       网页的「导入 .elf」会把任意文件喂进这条路径。 */
    if (eh->e_phentsize != sizeof(elf32_phdr_t)) {
        fprintf(stderr, "loader: e_phentsize=%u 不是 %zu\n",
                eh->e_phentsize, sizeof(elf32_phdr_t));
        free(buf); return false;
    }
    if (eh->e_phoff > (u32)sz ||
        (u64)eh->e_phoff + (u64)eh->e_phnum * sizeof(elf32_phdr_t) > (u64)sz) {
        fprintf(stderr, "loader: 程序头表越界\n"); free(buf); return false;
    }

    out->entry = eh->e_entry;

    /* 1) 装载 PT_LOAD 段 */
    for (u32 i = 0; i < eh->e_phnum; i++) {
        elf32_phdr_t *ph = (elf32_phdr_t *)(buf + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        /* 减法判上界：p_offset + p_filesz 在 u32 里会回绕 */
        if (ph->p_offset > (u32)sz || ph->p_filesz > (u32)sz - ph->p_offset) {
            fprintf(stderr, "loader: 段越界\n"); free(buf); return false;
        }
        if (ph->p_filesz > ph->p_memsz) {
            fprintf(stderr, "loader: p_filesz > p_memsz\n"); free(buf); return false;
        }
        u8 *dst = bus_ram();
        if (ph->p_paddr < RAM_BASE || (u64)ph->p_paddr + ph->p_memsz > (u64)RAM_BASE + RAM_SIZE) {
            fprintf(stderr, "loader: 段 0x%08x 超出 RAM\n", ph->p_paddr);
            free(buf); return false;
        }
        u32 off = ph->p_paddr - RAM_BASE;
        memcpy(dst + off, buf + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            memset(dst + off + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
        out->loaded_bytes += ph->p_memsz;
    }

    /* 2) 从符号表找 tohost / fromhost / begin_signature / end_signature */
    if (eh->e_shoff && eh->e_shnum) {
        /* 节头表同样来自文件头，先验界再算指针 */
        if (eh->e_shentsize != sizeof(elf32_shdr_t) ||
            eh->e_shoff > (u32)sz ||
            (u64)eh->e_shoff + (u64)eh->e_shnum * sizeof(elf32_shdr_t) > (u64)sz) {
            fprintf(stderr, "loader: 节头表越界\n"); free(buf); return false;
        }
        elf32_shdr_t *shs = (elf32_shdr_t *)(buf + eh->e_shoff);
        for (u32 i = 0; i < eh->e_shnum; i++) {
            elf32_shdr_t *sh = &shs[i];
            if (sh->sh_type != SHT_SYMTAB || sh->sh_entsize == 0) continue;
            if (sh->sh_link >= eh->e_shnum) continue;          /* 越界的 sh_link */
            if (sh->sh_offset > (u32)sz ||
                (u64)sh->sh_offset + (u64)sh->sh_size > (u64)sz) continue;
            elf32_shdr_t *strsh = &shs[sh->sh_link];
            if (strsh->sh_offset > (u32)sz ||
                (u64)strsh->sh_offset + (u64)strsh->sh_size > (u64)sz) continue;
            const char *strtab = (const char *)(buf + strsh->sh_offset);
            u32 n = sh->sh_size / sh->sh_entsize;
            for (u32 k = 0; k < n; k++) {
                elf32_sym_t *sym = (elf32_sym_t *)(buf + sh->sh_offset + k * sh->sh_entsize);
                if (sym->st_name >= strsh->sh_size) continue;  /* 越界的 st_name */
                const char *nm = strtab + sym->st_name;
                if (!strcmp(nm, "tohost"))              { out->tohost_addr = sym->st_value; out->has_tohost = true; }
                else if (!strcmp(nm, "fromhost"))       { out->fromhost_addr = sym->st_value; out->has_fromhost = true; }
                else if (!strcmp(nm, "begin_signature")) out->begin_signature = sym->st_value;
                else if (!strcmp(nm, "end_signature"))   out->end_signature = sym->st_value;
            }
            break;
        }
    }

    free(buf);
    return true;
}

bool loader_load_raw(const char *path, u32 addr, u32 *entry_out, u32 *size_out)
{
    long sz = 0;
    u8 *buf = read_file(path, &sz);
    if (!buf) { fprintf(stderr, "loader: 打不开 %s\n", path); return false; }
    if (addr < RAM_BASE || (u64)addr + (u64)sz > (u64)RAM_BASE + RAM_SIZE) {
        fprintf(stderr, "loader: 镜像超出 RAM\n"); free(buf); return false;
    }
    memcpy(bus_ram() + (addr - RAM_BASE), buf, (size_t)sz);
    if (entry_out) *entry_out = addr;
    if (size_out)  *size_out  = (u32)sz;
    free(buf);
    return true;
}