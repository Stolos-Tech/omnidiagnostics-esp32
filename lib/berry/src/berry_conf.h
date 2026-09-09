/********************************************************************
** Конфіг Berry для esp32-os. Базований на default/berry_conf.h,
** адаптований під ESP32/Arduino (Фаза 2).
**
** Змінено vs default:
**   - BE_USE_FILE_SYSTEM 0, BE_USE_OS_MODULE 0 — файлові/OS-операції
**     не переносимо на цьому етапі (realpath/dirent проблемні на newlib).
**     Завантаження .be з LittleFS прийде у Фазі 3 окремим шаром.
**   - BE_USE_BYTECODE_SAVER/LOADER 0, BE_USE_SHARED_LIB 0 — потрібні файли/dlopen.
**   - BE_USE_TIME_MODULE 0 — не потрібен зараз.
**   - JSON лишаємо: знадобиться для remote/protocol (Фаза 4).
********************************************************************/
#ifndef BERRY_CONF_H
#define BERRY_CONF_H

#include <assert.h>

#ifndef BE_DEBUG
#define BE_DEBUG                        0
#endif

#define BE_INTGER_TYPE                  2   /* long long */
#define BE_USE_SINGLE_FLOAT             0
#define BE_BYTES_MAX_SIZE               (32*1024)
#define BE_USE_PRECOMPILED_OBJECT       1
#define BE_DEBUG_SOURCE_FILE            1
#define BE_DEBUG_RUNTIME_INFO           1
#define BE_DEBUG_VAR_INFO               1
#define BE_USE_PERF_COUNTERS            1
#define BE_VM_OBSERVABILITY_SAMPLING    20
#define BE_STACK_TOTAL_MAX              20000
#define BE_STACK_FREE_MIN               10
#define BE_STACK_START                  50
#define BE_CONST_SEARCH_SIZE            50
#define BE_USE_STR_HASH_CACHE           0

/* Файлова система вимкнена на цьому етапі (див. шапку) */
#define BE_USE_FILE_SYSTEM              0

#define BE_USE_SCRIPT_COMPILER          1
/* SAVER/LOADER увімкнені: be_filelib.c::i_savecode безумовно посилається на
 * be_bytecode_save_to_fs, тож при 0 native-лінк (MinGW, без --gc-sections) падає
 * з undefined reference. Функції самодостатні (працюють з FILE* через be_fwrite),
 * справжня ФС їм не потрібна. На пристрої код відкидається dead-code elimination. */
#define BE_USE_BYTECODE_SAVER           1
#define BE_USE_BYTECODE_LOADER          1
#define BE_USE_SHARED_LIB               0
#define BE_USE_OVERLOAD_HASH            1
#define BE_USE_DEBUG_HOOK               0
#define BE_USE_DEBUG_GC                 0
#define BE_USE_DEBUG_STACK              0
#define BE_USE_MEM_ALIGNED              0

/* Модулі */
#define BE_USE_STRING_MODULE            1
#define BE_USE_JSON_MODULE              1   /* потрібен для remote/protocol (Фаза 4) */
#define BE_USE_MATH_MODULE              1
#define BE_USE_TIME_MODULE              0
#define BE_USE_OS_MODULE                0   /* залежить від файлової системи */
#define BE_USE_GLOBAL_MODULE            1
#define BE_USE_SYS_MODULE               1
#define BE_USE_DEBUG_MODULE             1
#define BE_USE_GC_MODULE                1
#define BE_USE_SOLIDIFY_MODULE          0
#define BE_USE_INTROSPECT_MODULE        1
#define BE_USE_STRICT_MODULE            1

#define BE_EXPLICIT_ABORT               abort
#define BE_EXPLICIT_EXIT                exit
#define BE_EXPLICIT_MALLOC              malloc
#define BE_EXPLICIT_FREE                free
#define BE_EXPLICIT_REALLOC             realloc

#define be_assert(expr)                 assert(expr)

#endif
