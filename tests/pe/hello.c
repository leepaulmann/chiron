// Freestanding Win32 test program for the loader and KERNEL32 shims. It links
// only against kernel32, like the game, and exits with a distinct code at the
// first failed check.
#include <windows.h>

static int counter = 5;
static int *volatile counter_ptr = &counter;  // needs a HIGHLOW relocation

static void put(const char *s) {
    DWORD n = 0, w;
    while (s[n]) n++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &w, NULL);
}

#define CHECK(code, cond)                \
    do {                                 \
        if (!(cond)) {                   \
            put("FAIL " #code "\n");     \
            ExitProcess(code);           \
        }                                \
    } while (0)

static volatile LONG thread_ran;
static DWORD WINAPI worker(void *arg) {
    InterlockedIncrement(&thread_ran);
    SetEvent((HANDLE)arg);
    return 7;
}

void __stdcall start(void) {
    CHECK(10, *counter_ptr == 5);
    CHECK(11, (GetVersion() & 0x80000000u) == 0);

    HANDLE heap = HeapCreate(0, 0, 0);
    char *p = HeapAlloc(heap, HEAP_ZERO_MEMORY, 100);
    CHECK(20, p && p[99] == 0 && HeapSize(heap, 0, p) == 100);
    p = HeapReAlloc(heap, 0, p, 5000);
    CHECK(21, p && HeapSize(heap, 0, p) == 5000);
    HeapFree(heap, 0, p);

    char *v = VirtualAlloc(NULL, 1 << 20, MEM_RESERVE, PAGE_NOACCESS);
    CHECK(22, v && VirtualAlloc(v + 4096, 4096, MEM_COMMIT, PAGE_READWRITE) == v + 4096);
    v[4096] = 1;
    CHECK(23, VirtualFree(v, 0, MEM_RELEASE));

    DWORD slot = TlsAlloc();
    CHECK(30, slot != TLS_OUT_OF_INDEXES && TlsSetValue(slot, (void *)0x1234));
    CHECK(31, TlsGetValue(slot) == (void *)0x1234);

    CRITICAL_SECTION cs;
    InitializeCriticalSection(&cs);
    EnterCriticalSection(&cs);
    EnterCriticalSection(&cs);  // recursive
    LeaveCriticalSection(&cs);
    LeaveCriticalSection(&cs);
    DeleteCriticalSection(&cs);

    HANDLE f = CreateFileA("Sub\\OUT.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CHECK(40, f == INVALID_HANDLE_VALUE);  // directory does not exist yet
    CHECK(41, CreateDirectoryA("sub", NULL));
    f = CreateFileA("Sub\\OUT.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CHECK(42, f != INVALID_HANDLE_VALUE);
    DWORD n;
    CHECK(43, WriteFile(f, "hello", 5, &n, NULL) && n == 5);
    CloseHandle(f);
    f = CreateFileA("C:\\SUB\\out.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    char buf[16] = {0};
    CHECK(44, f != INVALID_HANDLE_VALUE && GetFileSize(f, NULL) == 5);
    CHECK(45, ReadFile(f, buf, sizeof(buf), &n, NULL) && n == 5 && buf[4] == 'o');
    CHECK(46, SetFilePointer(f, 1, NULL, FILE_BEGIN) == 1 && ReadFile(f, buf, 1, &n, NULL) && buf[0] == 'e');
    CloseHandle(f);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("sub\\*.txt", &fd);
    CHECK(50, h != INVALID_HANDLE_VALUE && fd.nFileSizeLow == 5);
    CHECK(51, !FindNextFileA(h, &fd) && GetLastError() == ERROR_NO_MORE_FILES);
    FindClose(h);

    CHECK(60, GetPrivateProfileIntA("Alpha Centauri", "difficulty", 9, ".\\test.ini") == 3);
    CHECK(61, WritePrivateProfileStringA("New Section", "Key", "42", ".\\test.ini"));
    CHECK(62, GetPrivateProfileIntA("new section", "KEY", 0, ".\\test.ini") == 42);
    CHECK(63, GetPrivateProfileIntA("Alpha Centauri", "Difficulty", 9, ".\\test.ini") == 3);

    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, worker, ev, 0, NULL);
    CHECK(70, th && WaitForMultipleObjects(1, &ev, FALSE, 5000) == WAIT_OBJECT_0);
    CHECK(71, WaitForMultipleObjects(1, &th, TRUE, 5000) == WAIT_OBJECT_0 && thread_ran == 1);
    CHECK(72, WaitForMultipleObjects(1, &ev, FALSE, 10) == WAIT_TIMEOUT);  // auto-reset

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    CHECK(80, k32 && GetProcAddress(k32, "GetTickCount") != NULL);
    CHECK(81, GetProcAddress(k32, "NoSuchFunction") == NULL);

    put("PASS\n");
    ExitProcess(0);
}
