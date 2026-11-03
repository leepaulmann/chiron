// Freestanding Win32 test program for the layered drive C:. run_hello.sh
// gives it a read-only game folder holding TEST.INI, DATA\BASE.TXT and
// SAVES\OLD.SAV, and a separate user folder; it checks what the game sees,
// and the script then checks that only the user folder changed.
#include <windows.h>

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

static int read_all(const char *name, char *buf, DWORD size) {
    HANDLE f = CreateFileA(name, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    DWORD n = 0;
    if (f == INVALID_HANDLE_VALUE) return -1;
    ReadFile(f, buf, size - 1, &n, NULL);
    buf[n] = 0;
    CloseHandle(f);
    return (int)n;
}

static int count_files(const char *spec, char *first, DWORD first_size) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(spec, &fd);
    int n = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!n) first[0] = fd.cFileName[0];
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

void __stdcall start(void) {
    char buf[64], first[MAX_PATH];
    DWORD n;

    // The first write creates the user folder.
    HANDLE t = CreateFileA("first.tmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CHECK(5, t != INVALID_HANDLE_VALUE);
    CloseHandle(t);

    // Game files read as usual.
    CHECK(10, read_all("data\\base.txt", buf, sizeof(buf)) == 4 && buf[0] == 'b');

    // Changing the .ini keeps what the game folder's copy had.
    CHECK(20, WritePrivateProfileStringA("New Section", "Key", "42", ".\\test.ini"));
    CHECK(21, GetPrivateProfileIntA("new section", "KEY", 0, ".\\test.ini") == 42);
    CHECK(22, GetPrivateProfileIntA("Alpha Centauri", "Difficulty", 9, ".\\test.ini") == 3);

    // A new save sits next to the old one.
    HANDLE f = CreateFileA("saves\\New.sav", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CHECK(30, f != INVALID_HANDLE_VALUE && WriteFile(f, "x", 1, &n, NULL));
    CloseHandle(f);
    CHECK(31, count_files("saves\\*.sav", first, sizeof(first)) == 2 && first[0] == 'N');
    CHECK(32, SetCurrentDirectoryA("saves") && count_files("*.*", first, sizeof(first)) == 4);  // . .. New OLD
    CHECK(33, SetCurrentDirectoryA("C:\\"));

    // Writing into a game file changes the user folder's copy.
    f = CreateFileA("DATA\\BASE.TXT", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(40, f != INVALID_HANDLE_VALUE && WriteFile(f, "B", 1, &n, NULL));
    CloseHandle(f);
    CHECK(41, read_all("data\\base.txt", buf, sizeof(buf)) == 4 && buf[0] == 'B' && buf[1] == 'a');
    CHECK(42, CreateFileA("data\\base.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL) == INVALID_HANDLE_VALUE &&
                  GetLastError() == ERROR_FILE_EXISTS);

    // Folders: existing ones in either layer, new ones in the user folder.
    CHECK(50, !CreateDirectoryA("saves", NULL) && GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(51, CreateDirectoryA("mine", NULL) && SetCurrentDirectoryA("mine"));
    f = CreateFileA("note.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    CHECK(52, f != INVALID_HANDLE_VALUE);
    CloseHandle(f);
    CHECK(53, SetCurrentDirectoryA(".."));
    CHECK(54, CreateFileA("nowhere\\x.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL) == INVALID_HANDLE_VALUE);

    // Deleting: the game's files stay, the player's go.
    CHECK(60, !DeleteFileA("saves\\old.sav"));
    CHECK(61, DeleteFileA("saves\\new.sav") && count_files("saves\\*.sav", first, sizeof(first)) == 1);

    put("PASS\n");
    ExitProcess(0);
}
