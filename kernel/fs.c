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

#define FS_MAX_FILES 16
#define FS_NAME_MAX  24
#define FS_DATA_MAX  16384   /* fork_prog.bin 已 4272B，4096 装不下 */

typedef struct {
    u8  used;
    u8  is_dir;          /* 1 = 目录 */
    u8  owner;           /* 属主 uid */
    u8  mode;            /* 0 = 只有属主可写；1 = 所有人可写 */
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

/* 路径规范化：去掉开头的 '/'，其余原样保留。
   存储和查找都走这一份，名字才不会出现两种写法。 */
static int fs_norm(const char *in, char *out, int cap)
{
    int n = 0;
    if (!in) { out[0] = 0; return 0; }
    while (*in == '/') in++;
    while (in[n]) n++;
    if (n >= cap) { out[0] = 0; return -1; }   /* 太长必须报错，不能截断后当成功 */
    k_strcpy_n(out, in, cap);
    return 0;
}

static fs_file_t *fs_find(const char *name);   /* 定义在下面 */

/* 确保 path 的每一级父目录都存在。
   RAMFS 是平铺命名空间，写 a/b/c.txt 时如果不把 a、a/b 建成目录，
   这个文件就只能靠 shell 访问，图形界面永远进不去——数据在，入口没有。 */
static int fs_ensure_parents(const char *path)
{
    char buf[FS_NAME_MAX];
    int n = 0;
    while (path[n] && n < FS_NAME_MAX - 1) { buf[n] = path[n]; n++; }
    buf[n] = 0;
    for (int i = 1; i < n; i++) {
        if (buf[i] != '/') continue;
        buf[i] = 0;
        fs_file_t *d = fs_find(buf);
        if (d && !d->is_dir) return -1;      /* 父路径上是个文件，这条路径不成立 */
        fs_mkdir(buf);
        buf[i] = '/';
    }
    return 0;
}

/* ── 查找 ── */
static fs_file_t *fs_find(const char *name)
{
    char key[FS_NAME_MAX];
    if (!name || !name[0]) return 0;
    if (fs_norm(name, key, FS_NAME_MAX) < 0) return 0;
    if (!key[0]) return 0;
    for (u32 i = 0; i < FS_MAX_FILES; i++)
        if (g_files[i].used && k_streq(g_files[i].name, key)) return &g_files[i];
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
/* 当前写入者 uid：由 shell 在登录后设置 */
u32 g_cur_uid = 0;

int fs_write(const char *name, const u8 *data, u32 len)
{
    char key[FS_NAME_MAX];
    if (!name || !name[0]) return -1;
    if (fs_norm(name, key, FS_NAME_MAX) < 0) return -1;
    if (!key[0]) return -1;
    if (len > FS_DATA_MAX) return -1;
    if (fs_ensure_parents(key) < 0) return -1;

    fs_file_t *f = fs_find(key);
    if (f && f->is_dir) return -1;            /* 目录没有内容，别把文件写进目录项 */
    if (!f) {
        for (u32 i = 0; i < FS_MAX_FILES; i++) {
            if (!g_files[i].used) { f = &g_files[i]; break; }
        }
        if (!f) return -1;                    /* 槽位用尽 */
        f->used = 1;
        f->is_dir = 0;
        f->owner = (u8)g_cur_uid;
        f->mode  = 1;              /* 默认所有登录者可写，root 可改 */
        k_strcpy_n(f->name, key, FS_NAME_MAX);
    }
    for (u32 i = 0; i < len; i++) f->data[i] = data[i];
    f->size = len;
    return (int)len;
}

/* 读文件：返回长度；不存在返回 -1；超出 cap 截断。 */
int fs_read(const char *name, u8 *out, u32 cap)
{
    fs_file_t *f = fs_find(name);
    if (!f || f->is_dir) return -1;      /* 目录没有内容，读它是调用方的错 */
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
    char key[FS_NAME_MAX];
    fs_file_t *f = fs_find(name);
    if (!f) return -1;
    if (fs_norm(name, key, FS_NAME_MAX) < 0) return -1;
    if (f->is_dir) {
        int plen = k_strlen(key);
        for (u32 i = 0; i < FS_MAX_FILES; i++) {
            if (!g_files[i].used) continue;
            if (g_files[i].name[plen] != '/') continue;
            if (!fs_under(g_files[i].name, key)) continue;
            g_files[i].used = 0;
            g_files[i].name[0] = 0;
            g_files[i].size = 0;
        }
    }
    f->used = 0;
    f->name[0] = 0;
    f->size = 0;
    return 0;
}

/* 重命名 / 移动。RAMFS 是平铺命名空间，改名就是改字符串。
   目标已存在时失败，不做静默覆盖——覆盖掉的那个文件没有回收站。 */
int fs_rename(const char *from, const char *to)
{
    char key[FS_NAME_MAX], old[FS_NAME_MAX];
    fs_file_t *f = fs_find(from);
    if (!f || !to || !to[0]) return -1;
    if (fs_norm(to, key, FS_NAME_MAX) < 0) return -2;
    if (!key[0]) return -1;
    if (fs_norm(from, old, FS_NAME_MAX) < 0) return -1;
    if (k_streq(old, key)) return 0;            /* 改成自己：成功的无操作 */
    if (fs_find(key)) return -3;
    if (fs_ensure_parents(key) < 0) return -1;

    if (f->is_dir) {
        /* 目录改名要把所有子项的前缀一起换掉。
           只改目录项的话，子文件全留在旧路径下：看不见、进不去、也删不掉。 */
        int fl = k_strlen(old), tl = k_strlen(key);
        for (u32 i = 0; i < FS_MAX_FILES; i++) {
            if (!g_files[i].used) continue;
            if (g_files[i].name[fl] != '/') continue;
            if (!fs_under(g_files[i].name, old)) continue;
            int rest = k_strlen(g_files[i].name) - fl;
            if (tl + rest >= FS_NAME_MAX) continue;   /* 新前缀放不下就跳过 */
            char nb[FS_NAME_MAX];
            for (int k = 0; k < tl; k++) nb[k] = key[k];
            for (int k = 0; k <= rest; k++) nb[tl + k] = g_files[i].name[fl + k];
            k_strcpy_n(g_files[i].name, nb, FS_NAME_MAX);
        }
    }
    k_strcpy_n(f->name, key, FS_NAME_MAX);
    return 0;
}

/* 创建目录。已存在则返回 0（幂等）。 */
int fs_mkdir(const char *name)
{
    char key[FS_NAME_MAX];
    if (!name || !name[0]) return -1;
    if (fs_norm(name, key, FS_NAME_MAX) < 0) return -1;
    if (!key[0]) return -1;
    {
        fs_file_t *e = fs_find(key);
        if (e) return e->is_dir ? 0 : -2;     /* 同名文件存在：报冲突，别假装成功 */
    }
    if (fs_ensure_parents(key) < 0) return -1;

    for (u32 i = 0; i < FS_MAX_FILES; i++) {
        if (!g_files[i].used) {
            g_files[i].used = 1;
            g_files[i].is_dir = 1;
            g_files[i].owner = (u8)g_cur_uid;
            g_files[i].mode  = 1;
            g_files[i].size = 0;
            k_strcpy_n(g_files[i].name, key, FS_NAME_MAX);
            return 0;
        }
    }
    return -1;
}

/* 能否写：root 无条件；其他用户要求 mode=1 */
int fs_can_write(const char *name, u32 uid)
{
    fs_file_t *f = fs_find(name);
    if (!f) return 1;              /* 新建：允许 */
    if (uid == 0) return 1;
    return f->mode != 0;
}

int fs_owner(const char *name)
{
    fs_file_t *f = fs_find(name);
    return f ? (int)f->owner : -1;
}

int fs_chmod(const char *name, u32 mode)
{
    fs_file_t *f = fs_find(name);
    if (!f) return -1;
    f->mode = (u8)(mode & 1);
    return 0;
}

int fs_is_dir(const char *name)
{
    fs_file_t *f = fs_find(name);
    return f ? (int)f->is_dir : -1;
}

/* 前缀匹配：name 是否位于 dir 之下（dir 以 / 结尾或为空表示根）。
   支持 "/a" 与 "a" 两种写法。 */
int fs_under(const char *name, const char *dir)
{
    int dn = k_strlen(dir);
    if (dn == 0) return 1;                       /* 根目录：全部 */
    int i = 0;
    while (i < dn && name[i] && name[i] == dir[i]) i++;
    if (i != dn) return 0;
    /* dir 的下一字符必须是 '/' 或结尾，避免 /ab 匹配到 /abc */
    return name[dn] == '/' || name[dn] == 0;
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

int fs_entry_owner(u32 idx)
{
    if (idx >= FS_MAX_FILES || !g_files[idx].used) return -1;
    return (int)g_files[idx].owner;
}

/* 按名字取槽位下标（供 fd 表保存），不存在返回 -1 */
int fs_slot_of(const char *name)
{
    for (u32 i = 0; i < FS_MAX_FILES; i++)
        if (g_files[i].used && k_streq(g_files[i].name, name)) return (int)i;
    return -1;
}

/* 按槽位从 pos 开始读取（fd 的 read 用它实现顺序读） */
int fs_read_slot(u32 slot, u8 *out, u32 len, u32 pos)
{
    if (slot >= FS_MAX_FILES || !g_files[slot].used) return -1;
    fs_file_t *f = &g_files[slot];
    if (pos >= f->size) return 0;
    u32 n = f->size - pos;
    if (n > len) n = len;
    for (u32 i = 0; i < n; i++) out[i] = f->data[pos + i];
    return (int)n;
}

int fs_entry_type(u32 idx)
{
    if (idx >= FS_MAX_FILES || !g_files[idx].used) return -1;
    return g_files[idx].is_dir;
}
