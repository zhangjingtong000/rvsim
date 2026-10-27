/*
Copyright 2018 Embedded Microprocessor Benchmark Consortium (EEMBC)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

--- 修改说明 ---
本文件是由 EEMBC CoreMark 官方 bench/barebones/core_portme.c 改写而来，
用于 rvsim 模拟器的裸机 RISC-V 环境：替换了 stdio/clock 的实现，
改用 UART 输出与 CLINT mtime 计时。原始版权与许可以上保留。
*/

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

