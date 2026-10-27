#include <stdarg.h>
#include "coremark.h"
#include "core_portme.h"
#include "kernel.h"

ee_u32 default_num_contexts = 1;

ee_u32 portme_clock(void)
{
    volatile u32 *mt = (volatile u32 *)(CLINT_BASE + 0xBFF8u);
    return *mt;          /* mtime，1 MHz */
}

static CORE_TICKS g_start, g_stop;

void start_time(void) { g_start = (CORE_TICKS)portme_clock(); }
void stop_time(void)  { g_stop  = (CORE_TICKS)portme_clock(); }
CORE_TICKS get_time(void) { return g_stop - g_start; }

ee_u32 time_in_secs(ee_u32 ticks) { return ticks / CLOCKS_PER_SEC; }
ee_u32 time_in_msecs(ee_u32 ticks) { return (ticks % CLOCKS_PER_SEC) / 1000; }

void portable_init(core_portable *p, int *argc, char *argv[])
{
    (void)argc; (void)argv;
    if (sizeof(ee_ptr_int) != sizeof(ee_u8 *)) {
        ee_printf("ERROR! Please define ee_ptr_int to a type that holds a pointer!\n");
    }
    if (sizeof(ee_u32) != 4) {
        ee_printf("ERROR! Please define ee_u32 to a 32b unsigned type!\n");
    }
    p->portable_id = 1;
}

void portable_fini(core_portable *p)
{
    p->portable_id = 0;
}

