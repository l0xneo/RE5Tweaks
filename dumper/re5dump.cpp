#include <windows.h>
#include <vector>
#include <string>

static void SafeCopy(BYTE* d, const BYTE* s, SIZE_T n)
{
    __try { memcpy(d, s, n); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void Dump()
{
    auto base = (BYTE*)GetModuleHandleA(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    SIZE_T size = nt->OptionalHeader.SizeOfImage;
    std::vector<BYTE> buf(size, 0);
    for (SIZE_T off = 0; off < size;)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(base + off, &mbi, sizeof(mbi))) break;
        SIZE_T len = min((SIZE_T)((BYTE*)mbi.BaseAddress + mbi.RegionSize - (base + off)), size - off);
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        {
            SafeCopy(&buf[off], base + off, len);
        }
        off += len;
    }
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string p(path);
    p = p.substr(0, p.find_last_of('\\') + 1) + "re5dx9_dump.bin";
    HANDLE f = CreateFileA(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD w;
    WriteFile(f, buf.data(), (DWORD)size, &w, nullptr);
    CloseHandle(f);
    MessageBeep(MB_OK);
}

static DWORD WINAPI Worker(LPVOID)
{
    for (;;)
    {
        if (GetAsyncKeyState(VK_F12) & 1) Dump();
        Sleep(50);
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID)
{
    if (r == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    }
    return TRUE;
}
