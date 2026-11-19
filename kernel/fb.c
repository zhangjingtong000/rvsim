/* 帧缓冲与光栅化
 *
 * 关键约束：S 模式下一次朴素循环写像素约 3 条指令，12 MIPS 下全屏
 * 786k 像素要 197ms —— 所以「每帧重画整屏」这条路根本走不通。
 *
 * 这里的做法是：像素由 guest 画，但只画一次或只画变化的部分；
 * 窗口移动/滚动交给硬件的平面坐标与源偏移（改一个寄存器，约 20 条指令）。
 */
#include "kernel.h"
#include "cjk16.h"
#include "font8x16.h"

/* 平面描述（与模拟器 bus.h 的 disp_plane_t 一一对应） */
#define DISP_BASE_R    (DISP_BASE + 0x00u)
#define DISP_SCREEN_W  (DISP_BASE + 0x08u)
#define DISP_SCREEN_H  (DISP_BASE + 0x0Cu)
#define DISP_FORMAT    (DISP_BASE + 0x10u)
#define DISP_ENABLE    (DISP_BASE + 0x14u)
#define DISP_COMMIT    (DISP_BASE + 0x30u)
#define DISP_CURSOR_X  (DISP_BASE + 0x38u)
#define DISP_CURSOR_Y  (DISP_BASE + 0x3Cu)
#define DISP_CURSOR_EN (DISP_BASE + 0x40u)
#define DISP_MOUSE_X   (DISP_BASE + 0x44u)
#define DISP_MOUSE_Y   (DISP_BASE + 0x48u)
#define DISP_MOUSE_BTN (DISP_BASE + 0x4Cu)
#define DISP_MOUSE_EV  (DISP_BASE + 0x50u)
#define DISP_KEY_CODE  (DISP_BASE + 0x54u)
#define DISP_KEY_EV    (DISP_BASE + 0x60u)
#define DISP_PLANE(n)  (DISP_BASE + 0x100u + (n) * 0x40u)

#define W32(a)  (*(volatile u32 *)(a))
#define W8(a)   (*(volatile u8 *)(a))

u32 g_fb_w, g_fb_h;

/* ── 终端文本网格 ──
   UART 输出的字符同时写进这里，窗口里显示的就是真实的 shell 输出。
   只重画变化的那一行，避免每帧重绘整窗。 */
#define TERM_COLS 60
#define TERM_ROWS 21
/* 网格存 Unicode 码点：ASCII 是单宽，汉字是双宽。
   0 = 空，0xFFFFFFFF = 被左边宽字符占用的右半格。 */
static u32 g_term[TERM_ROWS][TERM_COLS];
static u8   g_term_dirty[TERM_ROWS];
static u32  g_term_cx, g_term_cy;

/* 终端网格的原点跟随窗口位置（窗口可拖动） */
typedef struct { u32 x, y, w, h; } win_rect_t;
extern win_rect_t *kernel_win0(void);
#define TERM_ORIGIN_X (kernel_win0()->x + 8)
#define TERM_ORIGIN_Y (kernel_win0()->y + 26)

void term_grid_reset(void)
{
    for (u32 y = 0; y < TERM_ROWS; y++) {
        for (u32 x = 0; x < TERM_COLS; x++) g_term[y][x] = ' ';
        g_term_dirty[y] = 0;          /* 直接清屏，不靠 flush（flush 跳过空格） */
    }
    g_term_cx = 0;
    g_term_cy = 0;

    u32 *buf = (u32 *)FB_BASE;
    fb_rect(buf, 800, 600, 800,
            TERM_ORIGIN_X, TERM_ORIGIN_Y,
            TERM_COLS * 8, TERM_ROWS * 16, 0x050608);
}

/* 在字模表里查码点；返回索引，找不到返回 -1 */
static int cjk_lookup(u32 cp)
{
    int lo = 0, hi = CJK_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_cjk_uni[mid] == cp) return mid;
        if (g_cjk_uni[mid] < cp) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/* 画一个 16x16 汉字（占两格宽） */
static void cjk_draw(u32 *buf, u32 stride, u32 x, u32 y, u32 cp, u32 fg, u32 bg, int draw_bg)
{
    int idx = cjk_lookup(cp);
    if (idx < 0) return;
    for (u32 j = 0; j < 16; j++) {
        u32 *row = buf + (y + j) * stride + x;
        u32 hi = g_cjk_bits[idx][j * 2];
        u32 lo = g_cjk_bits[idx][j * 2 + 1];
        for (u32 i = 0; i < 8; i++) {
            int on = (hi & (0x80u >> i)) != 0;
            row[i] = on ? fg : (draw_bg ? bg : row[i]);
        }
        for (u32 i = 0; i < 8; i++) {
            int on = (lo & (0x80u >> i)) != 0;
            row[8 + i] = on ? fg : (draw_bg ? bg : row[8 + i]);
        }
    }
}

/* 换行 + 滚屏。长行自动折行也必须走这里——
   否则 cy 会一直涨到 TERM_ROWS 然后卡死，所有字符挤在最后一行反复覆盖。 */
static void term_newline(void)
{
    g_term_cx = 0;
    g_term_cy++;
    if (g_term_cy >= TERM_ROWS) {
        for (u32 y = 1; y < TERM_ROWS; y++) {
            for (u32 x = 0; x < TERM_COLS; x++) g_term[y-1][x] = g_term[y][x];
            g_term_dirty[y-1] = 1;
        }
        for (u32 x = 0; x < TERM_COLS; x++) g_term[TERM_ROWS-1][x] = ' ';
        g_term_dirty[TERM_ROWS-1] = 1;
        g_term_cy = TERM_ROWS - 1;
    }
}

/* 把一个字符放进网格；返回 1 表示网格有变化 */
int term_grid_putc(char c)
{
    static u32 utf8_cp = 0;
    static int utf8_need = 0;

    if (c == '\r') { g_term_cx = 0; return 1; }
    if (c == '\n') { term_newline(); return 1; }
    if (c == '\b') {
        if (g_term_cx > 0) g_term_cx--;
        return 1;
    }

    u8 b = (u8)c;

    /* UTF-8 解码 */
    if (utf8_need) {
        if ((b & 0xC0) != 0x80) { utf8_need = 0; utf8_cp = 0; return 0; }
        utf8_cp = (utf8_cp << 6) | (b & 0x3F);
        if (--utf8_need) return 0;
        /* 拿到完整码点：能画就画，画不了就丢 */
        if (cjk_lookup(utf8_cp) >= 0) {
            if (g_term_cx + 1 >= TERM_COLS) term_newline();
            g_term[g_term_cy][g_term_cx] = utf8_cp;
            g_term[g_term_cy][g_term_cx + 1] = 0xFFFFFFFFu;   /* 右半格被占 */
            g_term_cx += 2;
            g_term_dirty[g_term_cy] = 1;
        }
        utf8_cp = 0;
        return 1;
    }
    if (b >= 0xF0) { utf8_cp = b & 0x07; utf8_need = 3; return 0; }
    if (b >= 0xE0) { utf8_cp = b & 0x0F; utf8_need = 2; return 0; }
    if (b >= 0xC0) { utf8_cp = b & 0x1F; utf8_need = 1; return 0; }

    if (b < 32) return 0;
    if (b > 126) return 0;

    if (g_term_cx >= TERM_COLS) term_newline();
    g_term[g_term_cy][g_term_cx++] = (u32)b;
    g_term_dirty[g_term_cy] = 1;
    return 1;
}

/* 把脏行画到 framebuffer */
/* 调试：把网格内容按行导出（非打印字符写成 .） */
void term_grid_dump(void)
{
    extern void uart_putc(char);
    extern void uart_puts(const char *);
    for (u32 y = 0; y < TERM_ROWS; y++) {
        uart_puts("[");
        if (y < 10) uart_putc('0');
        { char t[4]; u32 v = y; int n = 0;
          if (!v) { t[n++] = '0'; }
          while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
          for (int i = n - 1; i >= 0; i--) uart_putc(t[i]); }
        uart_puts("] ");
        for (u32 x = 0; x < TERM_COLS; x++) {
            char c = g_term[y][x];
            uart_putc((c >= 32 && c < 127) ? c : '.');
        }
        uart_putc('\n');
    }
}

/* 把所有行标记为脏，供整屏重画时调用 */
void term_grid_redraw_all(void)
{
    for (u32 y = 0; y < TERM_ROWS; y++) g_term_dirty[y] = 1;
}

u32 term_grid_flush(void)
{
    u32 dirty = 0;
    u32 *buf = (u32 *)FB_BASE;
    for (u32 y = 0; y < TERM_ROWS; y++) {
        if (!g_term_dirty[y]) continue;
        g_term_dirty[y] = 0;
        dirty++;
        /* 先清整行背景：否则旧字符不会被擦掉，新内容叠在旧内容上变成乱码 */
        fb_rect(buf, 800, 600, 800,
                TERM_ORIGIN_X, TERM_ORIGIN_Y + y * 16,
                TERM_COLS * 8, 16, 0x050608);
        for (u32 x = 0; x < TERM_COLS; x++) {
            u32 cp = g_term[y][x];
            if (cp == 0 || cp == ' ') continue;
            if (cp == 0xFFFFFFFFu) continue;          /* 宽字符右半格，左边已画 */
            if (cp > 127) {
                cjk_draw(buf, 800, TERM_ORIGIN_X + x * 8,
                         TERM_ORIGIN_Y + y * 16, cp, 0xc9d1d9, 0x050608, 1);
                continue;
            }
            char ch = (char)cp;
            u32 fg = (ch == '>') ? 0x5eead4 : ((ch == '?') ? 0xf87171 : 0xc9d1d9);
            fb_char(buf, 800, 600, 800,
                    TERM_ORIGIN_X + x * 8, TERM_ORIGIN_Y + y * 16,
                    ch, fg, 0x050608, 0);
        }
    }
    return dirty;
}
static u32 g_last_mouse_ev, g_last_key_ev;

typedef struct {
    u32 mouse_x, mouse_y, mouse_btn, mouse_ev;
    u32 key_code, key_ev;
} fb_input_t;

void fb_init(u32 w, u32 h)
{
    g_fb_w = w;
    g_fb_h = h;
    W32(DISP_SCREEN_W) = w;
    W32(DISP_SCREEN_H) = h;
    W32(DISP_FORMAT)   = 1;        /* 1 = 32bpp RGBA(内存序 B,G,R,A) */
    W32(DISP_ENABLE)   = 1;
}

/* 配置一个平面：addr 是 RAM 里的像素基址 */
void fb_plane(u32 n, u32 addr, u32 x, u32 y, u32 w, u32 h, u32 stride, u32 z)
{
    volatile u32 *p = (volatile u32 *)DISP_PLANE(n);
    p[0x00 / 4] = addr;
    p[0x04 / 4] = x;
    p[0x08 / 4] = y;
    p[0x0C / 4] = w;
    p[0x10 / 4] = h;
    p[0x14 / 4] = stride;
    p[0x18 / 4] = 0;        /* src_x */
    p[0x1C / 4] = 0;        /* src_y */
    p[0x20 / 4] = 1;        /* format: 32bpp */
    p[0x24 / 4] = z;
    p[0x28 / 4] = 0;
}

/* 让某个平面生效（d_seq 自增，前端据此重贴该平面） */
void fb_commit(u32 n)
{
    volatile u32 *p = (volatile u32 *)DISP_PLANE(n);
    p[0x3C / 4] = p[0x3C / 4];   /* 写 d_x1 触发提交 */
    W32(DISP_COMMIT) = 1;
}

/* 只改平面位置：拖动窗口的全部开销（~20 条指令） */
void fb_plane_move(u32 n, u32 x, u32 y)
{
    volatile u32 *p = (volatile u32 *)DISP_PLANE(n);
    p[0x04 / 4] = x;
    p[0x08 / 4] = y;
}

/* 只改源偏移：终端滚动一行 = 改一个寄存器，不是搬 900×600 像素 */
void fb_plane_scroll(u32 n, u32 src_y)
{
    volatile u32 *p = (volatile u32 *)DISP_PLANE(n);
    p[0x1C / 4] = src_y;
}

/* 颜色：0x00RRGGBB */
void fb_fill(u32 *buf, u32 w, u32 h, u32 stride, u32 color)
{
    for (u32 y = 0; y < h; y++) {
        u32 *row = buf + y * stride;
        for (u32 x = 0; x < w; x++) row[x] = color;
    }
}

/* 每轮 4 像素：实测 1.5 指令/像素，比朴素循环快一倍 */
void fb_fill_fast(u32 *buf, u32 w, u32 h, u32 stride, u32 color)
{
    for (u32 y = 0; y < h; y++) {
        u32 *row = buf + y * stride;
        u32 x = 0;
        for (; x + 4 <= w; x += 4) {
            row[x] = color; row[x+1] = color; row[x+2] = color; row[x+3] = color;
        }
        for (; x < w; x++) row[x] = color;
    }
}

/* 竖直渐变：壁纸用，一条带一次填充 */
void fb_gradient_v(u32 *buf, u32 w, u32 h, u32 stride, u32 top, u32 bottom)
{
    /* 16 条带：颜色按带算一次，带内直接填。
       逐行算渐变要 800 次除法（每次几十条指令），纯属浪费。 */
    const u32 BANDS = 16;
    for (u32 band = 0; band < BANDS; band++) {
        u32 y0 = band * h / BANDS;
        u32 y1 = (band + 1) * h / BANDS;
        u32 t = band * 255u / (BANDS - 1);
        u32 r = (((top >> 16) & 0xFF) * (255 - t) + ((bottom >> 16) & 0xFF) * t) / 255;
        u32 g = (((top >> 8) & 0xFF) * (255 - t) + ((bottom >> 8) & 0xFF) * t) / 255;
        u32 b = ((top & 0xFF) * (255 - t) + (bottom & 0xFF) * t) / 255;
        u32 c = (r << 16) | (g << 8) | b;
        for (u32 y = y0; y < y1; y++) {
            u32 *row = buf + y * stride;
            u32 x = 0;
            for (; x + 4 <= w; x += 4) { row[x]=c; row[x+1]=c; row[x+2]=c; row[x+3]=c; }
            for (; x < w; x++) row[x] = c;
        }
    }
}

void fb_rect(u32 *buf, u32 bw, u32 bh, u32 stride, u32 x, u32 y, u32 w, u32 h, u32 color)
{
    for (u32 j = 0; j < h && (y + j) < bh; j++) {
        u32 *row = buf + (y + j) * stride;
        u32 i = 0;
        /* 每轮 4 像素：逐像素循环约 3 条指令，展开后约 1.5 条 */
        for (; i + 4 <= w && (x + i + 3) < bw; i += 4) {
            row[x + i] = color; row[x + i + 1] = color;
            row[x + i + 2] = color; row[x + i + 3] = color;
        }
        for (; i < w && (x + i) < bw; i++) row[x + i] = color;
    }
}

/* 1px 边框 */
void fb_frame(u32 *buf, u32 bw, u32 bh, u32 stride,
              u32 x, u32 y, u32 w, u32 h, u32 color)
{
    if (w < 2 || h < 2) return;
    fb_rect(buf, bw, bh, stride, x, y, w, 1, color);
    fb_rect(buf, bw, bh, stride, x, y + h - 1, w, 1, color);
    fb_rect(buf, bw, bh, stride, x, y, 1, h, color);
    fb_rect(buf, bw, bh, stride, x + w - 1, y, 1, h, color);
}

/* 圆角矩形（3px 圆角，只切四角） */
void fb_round_rect(u32 *buf, u32 bw, u32 bh, u32 stride,
                   u32 x, u32 y, u32 w, u32 h, u32 color)
{
    fb_rect(buf, bw, bh, stride, x, y, w, h, color);
    /* 四角各切掉 3 个像素，形成轻微圆角 */
    static const u8 cut[3] = { 1, 2, 3 };
    for (u32 i = 0; i < 3; i++) {
        u32 n = cut[i];
        fb_rect(buf, bw, bh, stride, x, y + i, n, 1, 0);
        fb_rect(buf, bw, bh, stride, x + w - n, y + i, n, 1, 0);
        fb_rect(buf, bw, bh, stride, x, y + h - 1 - i, n, 1, 0);
        fb_rect(buf, bw, bh, stride, x + w - n, y + h - 1 - i, n, 1, 0);
    }
}

/* 读输入：返回 bit0=鼠标事件 bit1=键盘事件，坐标/按键写进 out */
u32 fb_input(fb_input_t *out)
{
    u32 r = 0;
    u32 mev = W32(DISP_MOUSE_EV);
    u32 kev = W32(DISP_KEY_EV);
    out->mouse_x = W32(DISP_MOUSE_X);
    out->mouse_y = W32(DISP_MOUSE_Y);
    out->mouse_btn = W32(DISP_MOUSE_BTN);
    out->mouse_ev = mev;
    out->key_code = W32(DISP_KEY_CODE);
    out->key_ev = kev;
    if (mev != g_last_mouse_ev) { g_last_mouse_ev = mev; r |= 1u; }
    if (kev != g_last_key_ev)   { g_last_key_ev = kev;   r |= 2u; }
    return r;
}

/* ── 字形查表 ──
   逐像素判断约 320 条指令/字符，全屏一刷就是几十万指令。
   这里把「一个位图字节 → 8 个像素颜色」预先算成表，
   每行只要 8 次 store（约 20 条指令），字符成本降到 1/3 以下。 */
/* 4 组颜色对缓存：终端一行里会在主色/强调色/错误色之间切换，
   只有一组缓存的话几乎每个字符都要重建 2048 项，整屏刷一次就要几十万指令。 */
#define LUT_SETS 16
static u32 g_lut[LUT_SETS][256][8];
static u32 g_lut_fg[LUT_SETS], g_lut_bg[LUT_SETS];
static int g_lut_next;

static u32 (*lut_build(u32 fg, u32 bg))[8]
{
    for (int k = 0; k < LUT_SETS; k++)
        if (g_lut_fg[k] == fg && g_lut_bg[k] == bg) return g_lut[k];

    int slot = g_lut_next;
    g_lut_next = (g_lut_next + 1) % LUT_SETS;
    for (u32 b = 0; b < 256; b++)
        for (u32 i = 0; i < 8; i++)
            g_lut[slot][b][i] = (b & (0x80u >> i)) ? fg : bg;
    g_lut_fg[slot] = fg;
    g_lut_bg[slot] = bg;
    return g_lut[slot];
}

/* 画一个字符：8x16 点阵 */
void fb_char(u32 *buf, u32 bw, u32 bh, u32 stride,
             u32 x, u32 y, char ch, u32 fg, u32 bg, int draw_bg)
{
    if (x + 8 > bw || y + 16 > bh) {
        /* 越界时退回逐像素，保证裁剪正确 */
        if (ch < 32 || ch > 126) ch = '?';
        const u8 *g = g_font8x16[(int)(ch - 32)];
        for (u32 j = 0; j < 16 && y + j < bh; j++) {
            u32 *row = buf + (y + j) * stride;
            u8 bits = g[j];
            for (u32 i = 0; i < 8 && x + i < bw; i++) {
                if (bits & (0x80u >> i)) row[x + i] = fg;
                else if (draw_bg) row[x + i] = bg;
            }
        }
        return;
    }

    if (ch < 32 || ch > 126) ch = '?';

    /* 纯空格且不画底色：窗口底本来就是该颜色，整块跳过 */
    if (ch == ' ' && !draw_bg) return;
    if (ch == ' ' && draw_bg) {
        for (u32 j = 0; j < 16; j++) {
            u32 *row = buf + (y + j) * stride + x;
            row[0]=bg; row[1]=bg; row[2]=bg; row[3]=bg;
            row[4]=bg; row[5]=bg; row[6]=bg; row[7]=bg;
        }
        return;
    }

    u32 (*lut)[8] = draw_bg ? lut_build(fg, bg) : lut_build(fg, fg);

    const u8 *g = g_font8x16[(int)(ch - 32)];
    for (u32 j = 0; j < 16; j++) {
        u32 *row = buf + (y + j) * stride + x;
        const u32 *src = lut[g[j]];
        if (draw_bg) {
            row[0]=src[0]; row[1]=src[1]; row[2]=src[2]; row[3]=src[3];
            row[4]=src[4]; row[5]=src[5]; row[6]=src[6]; row[7]=src[7];
        } else {
            /* 不透底色：只写点亮的像素 */
            u8 bits = g[j];
            if (!bits) continue;
            if (bits & 0x80) row[0] = fg;
            if (bits & 0x40) row[1] = fg;
            if (bits & 0x20) row[2] = fg;
            if (bits & 0x10) row[3] = fg;
            if (bits & 0x08) row[4] = fg;
            if (bits & 0x04) row[5] = fg;
            if (bits & 0x02) row[6] = fg;
            if (bits & 0x01) row[7] = fg;
        }
    }
}

/* 画一串字符，返回结束 x */
u32 fb_text(u32 *buf, u32 bw, u32 bh, u32 stride,
            u32 x, u32 y, const char *s, u32 fg, u32 bg, int draw_bg)
{
    u32 cx = x;
    for (const char *p = s; *p; p++) {
        fb_char(buf, bw, bh, stride, cx, y, *p, fg, bg, draw_bg);
        cx += 8;
    }
    return cx;
}

/* 读鼠标状态；返回 1 表示有新的按下/抬起事件 */
int fb_mouse_pos(u32 *x, u32 *y, u32 *btn, u32 *ev)
{
    static u32 last_ev = 0;
    u32 e = W32(DISP_MOUSE_EV);
    *x = W32(DISP_MOUSE_X);
    *y = W32(DISP_MOUSE_Y);
    *btn = W32(DISP_MOUSE_BTN);
    *ev = e;
    if (e == last_ev) return 0;
    last_ev = e;
    return 1;
}

void fb_cursor(u32 x, u32 y, u32 on)
{
    W32(DISP_CURSOR_X) = x;
    W32(DISP_CURSOR_Y) = y;
    W32(DISP_CURSOR_EN) = on;
}
