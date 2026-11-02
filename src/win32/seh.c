// Windows structured exception handling for game threads.
//
// A fault in Windows code (or a RaiseException call) is turned into an
// EXCEPTION_RECORD and CONTEXT on the faulting thread's own stack, and the
// handler chain at fs:[0] is walked exactly as Windows does: a handler may
// resume execution with a modified context, pass the exception on, or call
// RtlUnwind and jump to its __except block. With no taker, the filter set
// through SetUnhandledExceptionFilter gets the last word.
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include "../loader/modules.h"
#include "../loader/teb.h"
#include "../loader/thunks.h"
#include "win32.h"

typedef struct EXCEPTION_RECORD {
    DWORD code, flags;
    struct EXCEPTION_RECORD *next;
    void *address;
    DWORD nparams;
    DWORD info[15];
} EXCEPTION_RECORD;

typedef struct {
    DWORD flags;
    DWORD dr[6];
    BYTE float_save[112];
    DWORD gs, fs, es, ds;
    DWORD edi, esi, ebx, edx, ecx, eax;
    DWORD ebp, eip, cs, eflags, esp, ss;
    BYTE extended[512];
} CONTEXT;

_Static_assert(sizeof(EXCEPTION_RECORD) == 80, "EXCEPTION_RECORD layout");
_Static_assert(__builtin_offsetof(CONTEXT, edi) == 0x9c && sizeof(CONTEXT) == 0x2cc, "CONTEXT layout");

typedef struct {
    EXCEPTION_RECORD *record;
    CONTEXT *context;
} EXCEPTION_POINTERS;

struct seh_frame {
    struct seh_frame *prev;
    DWORD(__attribute__((cdecl)) * handler)(EXCEPTION_RECORD *, struct seh_frame *, CONTEXT *, void *);
};

#define SEH_END ((struct seh_frame *)0xffffffffu)
#define CONTEXT_FULL 0x10007
#define EXCEPTION_NONCONTINUABLE 0x1
#define EXCEPTION_UNWINDING 0x2
#define EXCEPTION_EXIT_UNWIND 0x4
#define STATUS_ACCESS_VIOLATION 0xc0000005u
#define STATUS_ILLEGAL_INSTRUCTION 0xc000001du
#define STATUS_INTEGER_DIVIDE_BY_ZERO 0xc0000094u
#define STATUS_INTEGER_OVERFLOW 0xc0000095u
#define STATUS_PRIVILEGED_INSTRUCTION 0xc0000096u
#define STATUS_FLOAT_INVALID 0xc0000090u
#define STATUS_NONCONTINUABLE_EXCEPTION 0xc0000025u

typedef LONG(WINAPI *TOP_LEVEL_FILTER)(EXCEPTION_POINTERS *);
static TOP_LEVEL_FILTER top_filter;

TOP_LEVEL_FILTER WINAPI SetUnhandledExceptionFilter(TOP_LEVEL_FILTER filter) {
    TOP_LEVEL_FILTER prev = top_filter;
    top_filter = filter;
    return prev;
}

// ---- Resuming a context ---------------------------------------------------
// Load every register from a CONTEXT and jump to its eip. eax, ecx, eflags
// and eip are staged just below the target esp, so the context itself must
// not lie there; contexts built here always leave a gap.
__attribute__((noreturn)) void seh_resume(const CONTEXT *ctx);
__asm__(".text\n"
        ".globl seh_resume\n"
        "seh_resume:\n"
        "    movl 4(%esp), %ecx\n"     // ecx = ctx
        "    movl 0xc4(%ecx), %eax\n"  // target esp
        "    subl $16, %eax\n"         // [eax]=eax [eax+4]=ecx [eax+8]=eflags [eax+12]=eip
        "    movl 0xb0(%ecx), %edx\n"
        "    movl %edx, 0(%eax)\n"
        "    movl 0xac(%ecx), %edx\n"
        "    movl %edx, 4(%eax)\n"
        "    movl 0xc0(%ecx), %edx\n"
        "    movl %edx, 8(%eax)\n"
        "    movl 0xb8(%ecx), %edx\n"
        "    movl %edx, 12(%eax)\n"
        "    movl 0x9c(%ecx), %edi\n"
        "    movl 0xa0(%ecx), %esi\n"
        "    movl 0xa4(%ecx), %ebx\n"
        "    movl 0xa8(%ecx), %edx\n"
        "    movl 0xb4(%ecx), %ebp\n"
        "    movl %eax, %esp\n"
        "    popl %eax\n"
        "    popl %ecx\n"
        "    popfl\n"
        "    ret\n");  // to eip; esp is now the target esp

// ---- Dispatching ----------------------------------------------------------

static void crash_report(const EXCEPTION_RECORD *rec, const CONTEXT *ctx) {
    char where[128];
    modules_describe_addr(ctx->eip, where, sizeof(where));
    fprintf(stderr, "chiron: unhandled exception 0x%08x at %s", rec->code, where);
    if (rec->code == STATUS_ACCESS_VIOLATION && rec->nparams >= 2)
        fprintf(stderr, " (%s address 0x%08x)", rec->info[0] ? "writing" : "reading", rec->info[1]);
    fprintf(stderr, "\n");
    thunk_trace_recent();
    modules_scan_stack(ctx->esp);
    fflush(stderr);
}

static int frame_valid(const struct seh_frame *f) {
    struct teb *t = teb_current();
    return f != SEH_END && ((uint32_t)f & 3) == 0 && (uint32_t)f >= t->stack_limit &&
           (uint32_t)f + sizeof(*f) <= t->stack_base;
}

// Walk the handler chain. Returns only if nobody handled the exception.
__attribute__((cdecl)) void seh_dispatch(EXCEPTION_RECORD *rec, CONTEXT *ctx) {
    struct teb *t = teb_current();
    for (struct seh_frame *f = (struct seh_frame *)t->exception_list; frame_valid(f); f = f->prev) {
        void *dispatcher_context = NULL;
        DWORD disposition = f->handler(rec, f, ctx, &dispatcher_context);
        if (disposition == 0) {  // ExceptionContinueExecution
            if (rec->flags & EXCEPTION_NONCONTINUABLE) {
                fprintf(stderr, "chiron: handler tried to continue a noncontinuable exception\n");
                break;
            }
            seh_resume(ctx);
        }
        // ExceptionContinueSearch (1), or a nested exception (2): keep looking.
    }
    if (top_filter) {
        EXCEPTION_POINTERS ep = {rec, ctx};
        LONG r = top_filter(&ep);
        if (r == -1) seh_resume(ctx);  // EXCEPTION_CONTINUE_EXECUTION
    }
    crash_report(rec, ctx);
    _exit(3);
}

// Called by the CRT's outermost __except filter: report the crash and let
// the CRT terminate the process.
LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS *ep) {
    if (top_filter) {
        LONG r = top_filter(ep);
        if (r) return r;
    }
    crash_report(ep->record, ep->context);
    return 1;  // EXCEPTION_EXECUTE_HANDLER
}

// RtlUnwind(target frame, target ip, record, return value): call each
// handler above the target with EXCEPTION_UNWINDING and unlink it.
void WINAPI RtlUnwind(struct seh_frame *target, void *target_ip, EXCEPTION_RECORD *rec, void *retval) {
    EXCEPTION_RECORD local = {.code = STATUS_NONCONTINUABLE_EXCEPTION};
    if (!rec) rec = &local;
    rec->flags |= EXCEPTION_UNWINDING | (target ? 0 : EXCEPTION_EXIT_UNWIND);
    CONTEXT ctx = {.flags = CONTEXT_FULL};
    struct teb *t = teb_current();
    struct seh_frame *f = (struct seh_frame *)t->exception_list;
    while (frame_valid(f) && f != target) {
        void *dispatcher_context = NULL;
        f->handler(rec, f, &ctx, &dispatcher_context);
        f = f->prev;
        t->exception_list = (uint32_t)f;
    }
    // Real RtlUnwind returns `retval` in eax; callers that use it pass 0.
    (void)retval;
}

// ---- RaiseException -------------------------------------------------------
// The entry point captures the caller's registers exactly, as C++ throw and
// longjmp-style handlers resume into them.

struct raise_regs {
    DWORD edi, esi, ebp, esp_unused, ebx, edx, ecx, eax, eflags, ret;
    DWORD code, flags, nargs;
    const DWORD *args;
};

__attribute__((cdecl)) void seh_raise(struct raise_regs *r) {
    EXCEPTION_RECORD rec = {.code = r->code, .flags = r->flags & EXCEPTION_NONCONTINUABLE, .address = (void *)r->ret};
    rec.nparams = r->nargs > 15 ? 15 : r->nargs;
    if (r->args) memcpy(rec.info, r->args, rec.nparams * 4);
    CONTEXT ctx = {.flags = CONTEXT_FULL};
    ctx.edi = r->edi, ctx.esi = r->esi, ctx.ebx = r->ebx, ctx.edx = r->edx, ctx.ecx = r->ecx, ctx.eax = r->eax;
    ctx.ebp = r->ebp, ctx.eflags = r->eflags, ctx.eip = r->ret;
    ctx.esp = (DWORD)&r->args + 4;  // the caller's esp once RaiseException has returned (stdcall)
    seh_dispatch(&rec, &ctx);
}

void WINAPI RaiseException(DWORD code, DWORD flags, DWORD nargs, const DWORD *args);  // export
__asm__(".text\n"
        ".globl RaiseException\n"
        "RaiseException:\n"
        "    pushfl\n"
        "    pushal\n"
        "    pushl %esp\n"
        "    call seh_raise\n"
        "    addl $4, %esp\n"  // only reached through a handler resuming into us
        "    popal\n"
        "    popfl\n"
        "    ret $16\n");

// ---- Faults ---------------------------------------------------------------

static DWORD fault_code(int sig, const siginfo_t *si) {
    switch (sig) {
    case SIGSEGV:
    case SIGBUS: return STATUS_ACCESS_VIOLATION;
    case SIGILL: return si->si_code == ILL_PRVOPC ? STATUS_PRIVILEGED_INSTRUCTION : STATUS_ILLEGAL_INSTRUCTION;
    case SIGFPE:
        if (si->si_code == FPE_INTDIV) return STATUS_INTEGER_DIVIDE_BY_ZERO;
        if (si->si_code == FPE_INTOVF) return STATUS_INTEGER_OVERFLOW;
        return STATUS_FLOAT_INVALID;
    }
    return STATUS_ILLEGAL_INSTRUCTION;
}

// Signal handler half: build the records below the faulting esp and make
// the thread "call" seh_dispatch when the handler returns.
int seh_handle_signal(int sig, siginfo_t *si, void *uctx) {
    ucontext_t *uc = uctx;
    greg_t *g = uc->uc_mcontext.gregs;
    if (!(g[REG_FS] & 4)) return 0;  // not a thread with a Windows TEB
    uint32_t sp = (g[REG_ESP] - 512) & ~15u;  // leave the red zone and a gap for seh_resume
    sp -= sizeof(CONTEXT);
    CONTEXT *ctx = (CONTEXT *)sp;
    sp -= sizeof(EXCEPTION_RECORD);
    EXCEPTION_RECORD *rec = (EXCEPTION_RECORD *)sp;

    memset(ctx, 0, sizeof(*ctx));
    ctx->flags = CONTEXT_FULL;
    ctx->gs = g[REG_GS], ctx->fs = g[REG_FS], ctx->es = g[REG_ES], ctx->ds = g[REG_DS];
    ctx->edi = g[REG_EDI], ctx->esi = g[REG_ESI], ctx->ebx = g[REG_EBX], ctx->edx = g[REG_EDX];
    ctx->ecx = g[REG_ECX], ctx->eax = g[REG_EAX], ctx->ebp = g[REG_EBP], ctx->eip = g[REG_EIP];
    ctx->cs = g[REG_CS], ctx->eflags = g[REG_EFL], ctx->esp = g[REG_ESP], ctx->ss = g[REG_SS];

    memset(rec, 0, sizeof(*rec));
    rec->code = fault_code(sig, si);
    rec->address = (void *)g[REG_EIP];
    if (rec->code == STATUS_ACCESS_VIOLATION) {
        rec->nparams = 2;
        rec->info[0] = (g[REG_ERR] & 2) ? 1 : 0;  // page-fault error code: write access
        rec->info[1] = (DWORD)si->si_addr;
    }
    if (modules_trace) {
        char where[128];
        modules_describe_addr(ctx->eip, where, sizeof(where));
        fprintf(stderr, "[seh] exception 0x%08x at %s\n", rec->code, where);
    }

    // Fake a cdecl call seh_dispatch(rec, ctx) with a null return address.
    uint32_t *stack = (uint32_t *)sp;
    *--stack = (uint32_t)ctx;
    *--stack = (uint32_t)rec;
    *--stack = 0;
    g[REG_ESP] = (greg_t)stack;
    g[REG_EIP] = (greg_t)seh_dispatch;
    g[REG_EFL] &= ~0x400;  // clear the direction flag, as C code expects
    return 1;
}
