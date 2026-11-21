/* ELF32 加载器（RISC-V）
 *
 * 之前程序是 objcopy 出来的裸二进制：没有段信息，只能整个 memcpy 到固定地址，
 * 而且内核只知道「代码从 0x80400000 开始」，不知道 .data 在哪里、BSS 多大。
 * 结果是 .data 落在第二页却没人映射，进程一访问就缺页拿到零页。
 *
 * 这里做真正的 ELF 加载：按 PT_LOAD 段复制到各自的虚拟地址，BSS 显式清零。
 * 支持 ELF32 / EM_RISCV / 无重定位（静态链接），足够跑正常的用户程序。
 */
#include "kernel.h"

#define PT_LOAD 1u

typedef struct {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u32 e_entry;
    u32 e_phoff;
    u32 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} elf32_hdr_t;

typedef struct {
    u32 p_type;
    u32 p_offset;
    u32 p_vaddr;
    u32 p_paddr;
    u32 p_filesz;
    u32 p_memsz;
    u32 p_flags;
    u32 p_align;
} elf32_phdr_t;

#define EM_RISCV 243u

/* 返回入口地址；失败返回 0。 */
u32 elf_load(const u8 *img, u32 len, u32 *entry_out, u32 *img_end_out)
{
    if (!img || len < sizeof(elf32_hdr_t)) return 0;

    const elf32_hdr_t *eh = (const elf32_hdr_t *)img;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') return 0;
    if (eh->e_ident[4] != 1) return 0;          /* 必须是 ELFCLASS32 */
    if (eh->e_ident[5] != 1) return 0;          /* 必须是小端 */
    if (eh->e_machine != EM_RISCV) return 0;
    if (eh->e_phentsize != sizeof(elf32_phdr_t)) return 0;

    u32 img_end = 0;

    for (u32 i = 0; i < eh->e_phnum; i++) {
        u32 off = eh->e_phoff + i * sizeof(elf32_phdr_t);
        if (off + sizeof(elf32_phdr_t) > len) return 0;

        const elf32_phdr_t *ph = (const elf32_phdr_t *)(img + off);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_filesz > ph->p_memsz) return 0;
        /* 用减法判上界：p_offset + p_filesz 在 u32 里会回绕，
           一个 p_offset=0xFFFFFFF0 的段能绕过原来的加法检查。 */
        if (ph->p_offset > len || ph->p_filesz > len - ph->p_offset) return 0;

        /* 链接脚本会把段起始对齐到 0x803ff000（页对齐），
           真正的入口仍在 0x80400000。这里放宽到 0x80300000，
           但映射阶段只给 >= 0x80400000 的部分加 U 位。 */
        if (ph->p_vaddr < 0x80300000u || ph->p_vaddr >= 0x80800000u) return 0;

        /* p_memsz 必须有上界。原来的代码直接
              for (k = 0; k < p_memsz; k++) *(u8*)(seg_va + k) = 0;
           一个 p_memsz = 0xFFFFFFFF 的 ELF 就是 4GB 的写循环，
           会把整个 RAM 和内核自己踩烂。网页的「导入 .elf」正好能把
           任意文件喂进这条路径，所以这不是理论问题。
           用户区一共 4 页（0x80400000..0x80800000），按 256KB 封顶足够。 */
        if (ph->p_memsz > 0x40000u) return 0;

        /* 链接脚本把段起始对齐到 0x803ff000，但那一页不属于用户映射。
           只写 >= 0x80400000 的部分：用户程序从入口开始执行，
           不需要 ELF header 页。若强行写 0x803ff000，在 g_kroot 下
           那页的映射与 0x80400000 不连续，跨页写入会落到错误物理页。 */
        u32 seg_va  = ph->p_vaddr;
        u32 seg_end = ph->p_vaddr + ph->p_memsz;
        if (seg_end < ph->p_vaddr) return 0;      /* 加法回绕 */
        if (seg_end > 0x80800000u) return 0;      /* 越出用户区 */
        u32 from = seg_va  < 0x80400000u ? 0x80400000u : seg_va;

        if (from < seg_end) {
            u32 skip = from - seg_va;             /* 段内偏移 */
            for (u32 k = skip; k < ph->p_filesz; k++)
                *(u8 *)(seg_va + k) = img[ph->p_offset + k];
            for (u32 k = (ph->p_filesz > skip ? ph->p_filesz : skip); k < ph->p_memsz; k++)
                *(u8 *)(seg_va + k) = 0;
        }

        if (seg_end > img_end) img_end = seg_end;
    }

    if (!img_end) return 0;
    if (entry_out) *entry_out = eh->e_entry;
    if (img_end_out) *img_end_out = img_end;
    return eh->e_entry;
}

/* 读 ELF 里所有 PT_LOAD 段的 [va, va+memsz)，供建立页表映射用。 */
int elf_segments(const u8 *img, u32 len, u32 *vas, u32 *ends, int maxseg)
{
    if (!img || len < sizeof(elf32_hdr_t)) return 0;
    const elf32_hdr_t *eh = (const elf32_hdr_t *)img;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') return 0;
    if (eh->e_ident[4] != 1 || eh->e_machine != EM_RISCV) return 0;

    int n = 0;
    for (u32 i = 0; i < eh->e_phnum && n < maxseg; i++) {
        u32 off = eh->e_phoff + i * sizeof(elf32_phdr_t);
        if (off + sizeof(elf32_phdr_t) > len) break;
        const elf32_phdr_t *ph = (const elf32_phdr_t *)(img + off);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        vas[n]  = ph->p_vaddr;
        ends[n] = ph->p_vaddr + ph->p_memsz;
        n++;
    }
    return n;
}
