/* 内存文件系统（RAMFS）
 *
 * 一个正常的操作系统至少要有「文件」这一等公民。此前这个内核只有进程和事件，
 * 所以前端也只能展示进程和事件——不是界面偷懒，是内核里没有别的东西。
 *
 * 这里做最小可用的内存文件系统：固定数量的文件槽，每个有名字和内容。
 * 不持久化（掉电即失），但足以让 shell、AI 和前端围绕「文件」构建出真正的界面：
 * 浏览、读写、删除、把日志写成文件、把配置存起来。
 */
#include "kernel.h"

#define FS_MAX_FILES 32
#define FS_NAME_MAX  24
#define FS_DATA_MAX  4096

typedef struct {
    u8  used;
    char name[FS_NAME_MAX];
    u32 size;
    u8  data[FS_DATA_MAX];
} fs_file_t;

static fs_file_t g_files[FS_MAX_FILES];

/* ── 字符串辅助（内核没有 libc）── */
static int k_strlen(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int k_streq(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void k_strcpy_n(char *dst, const char *src, int cap)
{
    int i = 0;
    while (i < cap - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void fs_init(void)
{
    for (u32 i = 0; i < FS_MAX_FILES; i++) {
        g_files[i].used = 0;
        g_files[i].name[0] = 0;
        g_files[i].size = 0;
    }
}

/* ── 查找 ── */
static fs_file_t *fs_find(const char *name)
{
    if (!name || !name[0]) return 0;
    for (u32 i = 0; i < FS_MAX_FILES; i++)
        if (g_files[i].used && k_streq(g_files[i].name, name)) return &g_files[i];
    return 0;
}

int fs_exists(const char *name) { return fs_find(name) != 0; }

u32 fs_count(void)
{
    u32 n = 0;
    for (u32 i = 0; i < FS_MAX_FILES; i++) if (g_files[i].used) n++;
    return n;
}

/* 写文件：不存在则创建，存在则覆盖。返回写入字节数，失败返回 -1。 */
int fs_write(const char *name, const u8 *data, u32 len)
{
    if (!name || !name[0]) return -1;
    if (k_strlen(name) >= FS_NAME_MAX) return -1;
    if (len > FS_DATA_MAX) return -1;

    fs_file_t *f = fs_find(name);
    if (!f) {
        for (u32 i = 0; i < FS_MAX_FILES; i++) {
            if (!g_files[i].used) { f = &g_files[i]; break; }
        }
        if (!f) return -1;                    /* 槽位用尽 */
        f->used = 1;
        k_strcpy_n(f->name, name, FS_NAME_MAX);
    }
    for (u32 i = 0; i < len; i++) f->data[i] = data[i];
    f->size = len;
    return (int)len;
}

/* 读文件：返回长度；不存在返回 -1；超出 cap 截断。 */
int fs_read(const char *name, u8 *out, u32 cap)
{
    fs_file_t *f = fs_find(name);
    if (!f) return -1;
    u32 n = f->size;
    if (n > cap) n = cap;
    for (u32 i = 0; i < n; i++) out[i] = f->data[i];
    return (int)n;
}

/* 返回文件大小；不存在返回 -1 */
int fs_size(const char *name)
{
    fs_file_t *f = fs_find(name);
    return f ? (int)f->size : -1;
}

/* 删除：成功返回 0，不存在返回 -1 */
int fs_remove(const char *name)
{
    fs_file_t *f = fs_find(name);
    if (!f) return -1;
    f->used = 0;
    f->name[0] = 0;
    f->size = 0;
    return 0;
}

/* 按下标枚举（供 ls 使用）。返回 1 表示该下标记存在，名字写入 out。 */
int fs_entry(u32 idx, char *out, int cap, u32 *size)
{
    if (idx >= FS_MAX_FILES) return 0;
    if (!g_files[idx].used) return 0;
    k_strcpy_n(out, g_files[idx].name, cap);
    if (size) *size = g_files[idx].size;
    return 1;
}
