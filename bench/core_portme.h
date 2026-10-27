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

#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h>

typedef unsigned int   ee_u32;
typedef signed int     ee_s32;
typedef unsigned short ee_u16;
typedef signed short   ee_s16;
typedef unsigned char  ee_u8;
typedef signed char    ee_s8;
typedef double         ee_f32;
typedef double         ee_f64;
typedef size_t         ee_size_t;
typedef ee_u32         ee_ptr_int;
typedef ee_u32         CORE_TICKS;

#define COMPILER_VERSION "riscv64-unknown-elf-gcc"
#define COMPILER_FLAGS   "-O2 -march=rv32ima_zicsr_zifencei"

#define MEM_LOCATION "STATIC"
#define MEM_METHOD MEM_STATIC
#ifndef TOTAL_DATA_SIZE
#define TOTAL_DATA_SIZE (64 * 1024)
#endif

#define HAS_FLOAT   0
#define HAS_TIME_H  0
#define USE_CLOCK   1
#define HAS_PRINTF  0
#define SEED_METHOD SEED_VOLATILE
#ifndef STANDALONE
#define STANDALONE 1
#endif
#ifndef ITERATIONS
#define ITERATIONS 400
#endif
#ifndef PERFORMANCE_RUN
#define PERFORMANCE_RUN 1
#endif

#define MULTITHREAD 1
#define MAIN_HAS_NOARGC 1
#define MAIN_HAS_NORETURN 0

extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S {
    ee_u8 portable_id;
} core_portable;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) && !defined(VALIDATION_RUN)
#define PERFORMANCE_RUN 1
#endif

ee_u32 portme_clock(void);
#define CLOCKS_PER_SEC 10000000


ee_u32 time_in_secs(ee_u32 ticks);
ee_u32 time_in_msecs(ee_u32 ticks);


/* ── 本移植层提供的函数声明 ──
   官方 CoreMark 在 barebones 移植里由 core_portme.h 提供这些原型；
   缺了会退化成隐式声明（C 默认返回 int），ee_printf 的返回值被截断、参数不检查。 */
int  ee_printf(const char *fmt, ...);
void *align_mem(void *p);

#endif