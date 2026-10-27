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

#endif