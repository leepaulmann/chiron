// Structured exception handling test for Chiron, using hand-registered SEH
// frames (mingw's GCC has no __try for x86). Exits with a distinct code at
// the first failed check.
#include <windows.h>

static void put(const char *s) {
    DWORD n = 0, w;
    while (s[n]) n++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &w, NULL);
}

#define CHECK(code, cond)            \
    do {                             \
        if (!(cond)) {               \
            put("FAIL " #code "\n"); \
            ExitProcess(code);       \
        }                            \
    } while (0)

struct frame {
    struct frame *prev;
    void *handler;
};

static void push_frame(struct frame *f, void *handler) {
    f->handler = handler;
    __asm__ volatile("movl %%fs:0, %%eax; movl %%eax, (%0); movl %0, %%fs:0" ::"r"(f) : "eax", "memory");
}

static void pop_frame(struct frame *f) { __asm__ volatile("movl %0, %%fs:0" ::"r"(f->prev) : "memory"); }

static struct frame *current_frame(void) {
    struct frame *f;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(f));
    return f;
}

// The faulting instructions below have known lengths, so handlers can step
// over them: "movl $0x2a, (%eax)" is 6 bytes, "divl %ecx" is 2.
static void fault_write_null(void) {
    __asm__ volatile("xorl %%eax, %%eax\n\tmovl $0x2a, (%%eax)" ::: "eax", "memory");
}

static int divide_by_zero(void) {
    int r;
    __asm__ volatile("movl $1, %%eax\n\txorl %%edx, %%edx\n\txorl %%ecx, %%ecx\n\tdivl %%ecx"
                     : "=a"(r)::"ecx", "edx");
    return r;
}

static int av_seen, av_write, av_addr_ok;
static EXCEPTION_DISPOSITION __cdecl av_handler(EXCEPTION_RECORD *rec, void *frame, CONTEXT *ctx, void *d) {
    if (rec->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return ExceptionContinueSearch;
    av_seen++;
    av_write = rec->NumberParameters >= 2 && rec->ExceptionInformation[0] == 1;
    av_addr_ok = rec->ExceptionInformation[1] == 0;
    ctx->Eip += 6;
    return ExceptionContinueExecution;
}

static int div_seen;
static EXCEPTION_DISPOSITION __cdecl div_handler(EXCEPTION_RECORD *rec, void *frame, CONTEXT *ctx, void *d) {
    if (rec->ExceptionCode != EXCEPTION_INT_DIVIDE_BY_ZERO) return ExceptionContinueSearch;
    div_seen++;
    ctx->Eax = 77;  // the result the interrupted code will see
    ctx->Eip += 2;
    return ExceptionContinueExecution;
}

static int raise_seen;
static EXCEPTION_DISPOSITION __cdecl raise_handler(EXCEPTION_RECORD *rec, void *frame, CONTEXT *ctx, void *d) {
    if (rec->ExceptionCode != 0xe0001234) return ExceptionContinueSearch;
    if (rec->NumberParameters == 2 && rec->ExceptionInformation[0] == 11 && rec->ExceptionInformation[1] == 22)
        raise_seen++;
    return ExceptionContinueExecution;
}

static int inner_calls, inner_unwinds;
static EXCEPTION_DISPOSITION __cdecl inner_handler(EXCEPTION_RECORD *rec, void *frame, CONTEXT *ctx, void *d) {
    inner_calls++;
    if (rec->ExceptionFlags & EXCEPTION_UNWINDING) inner_unwinds++;
    return ExceptionContinueSearch;
}

static int outer_seen;
static EXCEPTION_DISPOSITION __cdecl outer_handler(EXCEPTION_RECORD *rec, void *frame, CONTEXT *ctx, void *d) {
    if (rec->ExceptionFlags & EXCEPTION_UNWINDING) return ExceptionContinueSearch;
    outer_seen++;
    RtlUnwind(frame, 0, rec, 0);  // runs inner_handler with EXCEPTION_UNWINDING, unlinks it
    ctx->Eip += 6;
    return ExceptionContinueExecution;
}

static int filter_seen;
static LONG WINAPI top_filter(EXCEPTION_POINTERS *ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    filter_seen++;
    ep->ContextRecord->Eip += 6;
    return EXCEPTION_CONTINUE_EXECUTION;
}

void __stdcall start(void) {
    struct frame f;
    volatile int keep = 1234;

    push_frame(&f, av_handler);
    fault_write_null();
    pop_frame(&f);
    CHECK(10, av_seen == 1 && av_write && av_addr_ok && keep == 1234);

    push_frame(&f, div_handler);
    int r = divide_by_zero();
    pop_frame(&f);
    CHECK(20, div_seen == 1 && r == 77);

    push_frame(&f, raise_handler);
    DWORD args[2] = {11, 22};
    RaiseException(0xe0001234, 0, 2, args);
    pop_frame(&f);
    CHECK(30, raise_seen == 1 && keep == 1234);

    // Both frames must live on the stack: like Windows, Chiron ignores
    // exception frames outside the thread's stack.
    struct frame outer, inner;
    push_frame(&outer, outer_handler);
    push_frame(&inner, inner_handler);
    fault_write_null();
    CHECK(40, outer_seen == 1 && inner_calls == 2 && inner_unwinds == 1);
    // Compare as plain numbers through a volatile: GCC otherwise assumes a
    // pointer produced by inline assembly can never equal a local's address.
    volatile uintptr_t cur = (uintptr_t)current_frame();
    CHECK(41, cur == (uintptr_t)&outer);  // RtlUnwind unlinked the inner frame
    pop_frame(&outer);

    SetUnhandledExceptionFilter(top_filter);
    fault_write_null();  // no frames: only the filter can handle this
    CHECK(50, filter_seen == 1);

    put("PASS\n");
    ExitProcess(0);
}
