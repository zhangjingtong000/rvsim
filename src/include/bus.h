#ifndef RV_BUS_H
#define RV_BUS_H

#include "rv_types.h"
#include "csr.h"

/* 设备状态 */
typedef struct {
    u64 mtimecmp;
    u32 msip;
} clint_t;

#define UART_RX_FIFO 256
typedef struct {
    u8  rbr;        /* 当前接收字节 */
    u8  ier;
    u8  lcr;
    u8  mcr;
    u8  lsr;        /* bit5=THRE, bit0=DR */
    bool rx_pending;
    u8   fifo[UART_RX_FIFO];
    u32  fifo_head, fifo_tail;
} uart_t;

typedef struct {
    u32 priority[PLIC_NDEV + 1];
    u32 pending;
    u32 enable;
    u32 threshold;
    u32 claimed;
} plic_t;

/* 显示控制器：8 个平面，前端按 z 序合成。
   内核画像素，宿主只做矩形搬运——等价于真实的 KMS 平面合成。 */
#define DISP_PLANES 8

typedef struct {
    u32 fb_addr, x, y, w, h, stride;
    u32 src_x, src_y;
    u32 format, zorder, flags;
    u32 d_seq;
    u32 d_x0, d_y0, d_x1, d_y1;
} disp_plane_t;

typedef struct {
    u32 w, h, format, enable;
    u32 frame_seq;
    u32 dmg_x0, dmg_y0, dmg_x1, dmg_y1;
    u32 cursor_x, cursor_y, cursor_en;
    u32 mouse_x, mouse_y, mouse_btn, mouse_ev;
    u32 key_code, key_scan, key_mods, key_ev;
    disp_plane_t plane[DISP_PLANES];
} disp_t;

typedef struct {
    bool poweroff;
    bool reboot;
    u32  exit_code;
} syscon_t;

extern clint_t  g_clint;
extern uart_t   g_uart;
extern disp_t   g_disp;
extern plic_t   g_plic;
extern syscon_t g_syscon;

/* 检测到登录提示符后置位，供宿主喂入凭据 */
extern bool g_login_seen;
extern bool g_shell_prompt_seen;
extern bool g_password_seen;

void  bus_init(void);
u8   *bus_ram(void);
void  disp_init(void);
/* 返回 false 表示访问未映射（调用方决定是否 access fault） */
bool  bus_read(u32 addr, int len, u32 *val);
bool  bus_write(u32 addr, int len, u32 val);

/* 由主循环推进：更新 mtime、CLINT 中断、PLIC → mip */
void  bus_tick(csr_t *csr, u64 cycles);
/* 从中断控制器汇总出 mip 的硬件位 */
void  bus_update_irqs(csr_t *csr);

u32  bus_hot_read(u32 page);
u32  bus_hot_write(u32 page);
void bus_hot_clear(void);

void uart_push_rx(u8 byte);

/* UART 输出环形缓冲：供 WASM/前端取走 guest 打印的字符。
   原生跑的时候这些字符同时打到 stdout，这里只是多存一份。 */
int  bus_uart_out_take(u8 *dst, int max);   /* 取走并返回字节数 */
void bus_uart_out_clear(void);
int  bus_uart_out_pending(void);

#endif /* RV_BUS_H */
