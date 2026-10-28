// metal/include/metal_compat/immintrin.h - the x86 intrinsics the host loops use, on arm64: the spin-wait hint and
// the store fence before a host flag is raised (verify.cpp, session.cpp, mtp.cpp, graph.cpp).  On an Intel Mac the
// real <immintrin.h> is right: it has AVX2 and the x86 fence.
#pragma once
#if defined(__aarch64__) || defined(__arm64__)
static inline void _mm_pause() { __asm__ __volatile__("yield"); }
static inline void _mm_sfence() { __asm__ __volatile__("dmb ishst" ::: "memory"); }
#else
#include_next <immintrin.h>
#endif
