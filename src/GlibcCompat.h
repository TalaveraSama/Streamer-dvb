#pragma once
// Compatibilidad de glibc: mallinfo2() aparece en glibc 2.33; Ubuntu 20.04
// usa glibc 2.31. Se emula con mallinfo() (campos de 32 bits, suficiente para
// las metricas informativas del panel; 22.04/24.04 usan la funcion real).
#include <cstddef>
#include <malloc.h>

#if defined(__GLIBC__) && !__GLIBC_PREREQ(2, 33)
struct mallinfo2 {
    std::size_t arena;
    std::size_t ordblks;
    std::size_t smblks;
    std::size_t hblks;
    std::size_t hblkhd;
    std::size_t usmblks;
    std::size_t fsmblks;
    std::size_t uordblks;
    std::size_t fordblks;
    std::size_t keepcost;
};

static inline struct mallinfo2 mallinfo2() {
    const struct mallinfo legacy = mallinfo();
    struct mallinfo2 converted{};
    converted.arena = static_cast<std::size_t>(legacy.arena);
    converted.ordblks = static_cast<std::size_t>(legacy.ordblks);
    converted.smblks = static_cast<std::size_t>(legacy.smblks);
    converted.hblks = static_cast<std::size_t>(legacy.hblks);
    converted.hblkhd = static_cast<std::size_t>(legacy.hblkhd);
    converted.usmblks = static_cast<std::size_t>(legacy.usmblks);
    converted.fsmblks = static_cast<std::size_t>(legacy.fsmblks);
    converted.uordblks = static_cast<std::size_t>(legacy.uordblks);
    converted.fordblks = static_cast<std::size_t>(legacy.fordblks);
    converted.keepcost = static_cast<std::size_t>(legacy.keepcost);
    return converted;
}
#endif
