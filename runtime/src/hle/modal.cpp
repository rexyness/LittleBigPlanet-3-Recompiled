#include "hle/modal.h"

#include <atomic>
#include <cstdio>

namespace hle::modal {
namespace {

std::atomic<int> g_open{0};

}  // namespace

void Begin(const char* who) {
    const int n = ++g_open;
    std::fprintf(stderr, "[modal] %s opened (%d open)\n", who ? who : "?", n);
}

void End() {
    int n = g_open.load();
    while (n > 0 && !g_open.compare_exchange_weak(n, n - 1)) {
    }
    std::fprintf(stderr, "[modal] closed (%d open)\n", n > 0 ? n - 1 : 0);
}

bool Active() { return g_open.load() > 0; }

}  // namespace hle::modal
