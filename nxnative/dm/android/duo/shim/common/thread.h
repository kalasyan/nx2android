// nx2android: Eden's thread helpers the runtime uses
#pragma once
#include <pthread.h>
#include <string>
#include "common/common_types.h"
#include "common/polyfill_thread.h"
namespace Common {
enum class ThreadPriority : u32 { Low = 0, Normal = 1, High = 2, VeryHigh = 3, Critical = 4 };
void SetCurrentThreadPriority(ThreadPriority new_priority);
inline void SetCurrentThreadName(const char* name) { pthread_setname_np(pthread_self(), std::string(name).substr(0, 15).c_str()); }
}
