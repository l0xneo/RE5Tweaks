#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <d3d9.h>
#include <dinput.h>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_dx9.h"
#include "imgui/backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static float g_sens = 1.0f;
static bool g_freeCam = true;
static bool g_rawMouse = true;
static bool g_rawAim = true;
static int g_qteKey = 0;
static int g_qteKey2 = 0;
static float g_fovAdd = 0.0f;
static bool g_ultrawide = true;
static bool g_keepPitch = true;
static bool g_levelAim = true;
static bool g_dpi = true;
static bool g_noVsync = false;
static bool g_laser = false;
static float g_dotSize = 4.0f;
static float g_dotEnemy = 7.0f;
static BYTE g_laserEnemy;
static BYTE g_laserVis;
static DWORD g_laserSeq;
static DWORD g_laserSeen;
static DWORD g_laserLast;
static bool g_borderless = false;
static float g_music = 1.0f;
static float g_sfx = 1.0f;
static float g_cut = 1.0f;
static bool g_skipLogos = true;
static int g_toggleKey = VK_INSERT;
static bool g_noFilter = false;
static bool g_resLimit = true;
static int g_fpsCap = 0;
static float g_fpsCapF = 120.0f;
static int g_shadow = 0;

static char g_ini[MAX_PATH];
static bool g_armed = true;
static BYTE g_res;
static BYTE* g_player;
static int g_bbW, g_bbH;
static bool g_menu;
static bool g_imgui;
static bool g_toggleDown;
static HWND g_hwnd;
static WNDPROC g_wndProc;
static CRITICAL_SECTION g_cs;
__declspec(align(16)) static BYTE g_fx[512];

typedef void(__thiscall* AngleFn)(void*, float*);
static AngleFn GetAngle = (AngleFn)0x48a7f0;
static AngleFn SetAngle = (AngleFn)0x489ce0;
typedef void*(__thiscall* PerspFn)(void*, float, float, float, float);
static PerspFn Perspective = (PerspFn)0x6d1650;

static const DWORD kMouseSite = 0xb8c6e8;
static const DWORD kMouseRet = 0xb8c6f0;
static const DWORD kMouseSkip = 0xb8c70e;
static const DWORD kQteJoin = 0x76714b;
static const DWORD kQteBRet = 0x7670fd;
static const DWORD kQteA[] = { 0x767000, 0x767016, 0x76702c, 0x767042, 0x767054 };
static const DWORD kQteB = 0x7670f8;
static const DWORD kPerspGame = 0x835c89;
static const DWORD kPerspOther[] = { 0x4d6901, 0x5821a9 };
static const DWORD kCatVol = 0x48e680;
static const DWORD kRawAim = 0xb85fba;
static const DWORD kLaserLine = 0xb77153;
static const DWORD kLaserStart = 0xb76ea3;
static const DWORD kLaserHit = 0xb771a6;
static const DWORD kLaserHitRet = 0xb771ac;
static const DWORD kLogoBoot = 0xbfbb1c;
static const DWORD kLogoBootSkip = 0xbfbd44;
static const DWORD kLogoMovie = 0xbfb8d6;
static const DWORD kFov = 0x846e8c;
static const DWORD kFovRet = 0x846e94;
static const DWORD kUiScale[] = { 0x5de90f, 0x5dd236 };
static const DWORD kFilter = 0x47bd08;
static const DWORD kFps = 0x42706c;
static const DWORD kFpsRet = 0x427074;
static const DWORD kShadow = 0x63c8a5;
static const DWORD kShadowRet = 0x63c8ae;

static int* g_bind;
static bool g_hasRawAim, g_hasLaser, g_hasFilter, g_hasUi, g_hasPitch;
static const DWORD kRunRecenter = 0x845acd;
static const DWORD kAimStart = 0xb84cc2;
static const DWORD kAimStartRet = 0xb84ccc;
static char g_uiOrig[2][13];

static void Write(DWORD at, const void* src, int len)
{
    DWORD old;
    VirtualProtect((void*)at, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)at, src, len);
    VirtualProtect((void*)at, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, len);
}

static void Hex(DWORD at, const char* hex)
{
    BYTE b[32];
    int n = 0;
    for (; hex[n * 2]; n++)
    {
        char c[3] = { hex[n * 2], hex[n * 2 + 1], 0 };
        b[n] = (BYTE)strtoul(c, nullptr, 16);
    }
    Write(at, b, n);
}

static void Jmp(DWORD at, void* to, int len)
{
    BYTE b[16];
    b[0] = 0xE9;
    *(DWORD*)(b + 1) = (DWORD)to - at - 5;
    for (int i = 5; i < len; i++) b[i] = 0x90;
    Write(at, b, len);
}

static void CallRel(DWORD at, void* to)
{
    DWORD rel = (DWORD)to - at - 5;
    Write(at + 1, &rel, 4);
}

static bool Match(DWORD at, const char* hex)
{
    __try
    {
        BYTE* p = (BYTE*)at;
        for (int i = 0; hex[i * 2]; i++)
        {
            char b[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
            if (p[i] != (BYTE)strtoul(b, nullptr, 16)) return false;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool IsCall(DWORD at, DWORD target)
{
    return *(BYTE*)at == 0xE8 && at + 5 + *(DWORD*)(at + 1) == target;
}

static void ApplyRawAim()
{
    if (g_hasRawAim) Hex(kRawAim, g_rawAim ? "eb" : "76");
}

static void ApplyLaser()
{
    if (!g_hasLaser) return;
    Hex(kLaserLine, g_laser ? "9090" : "32db");
    Hex(kLaserStart, g_laser ? "e9dc01000090" : "0f84db010000");
}

static float ScreenAspect()
{
    if (g_bbW > 0 && g_bbH > 0) return (float)g_bbW / (float)g_bbH;
    return (float)GetSystemMetrics(SM_CXSCREEN) / (float)GetSystemMetrics(SM_CYSCREEN);
}

static void ApplyPitch()
{
    if (g_hasPitch) Hex(kRunRecenter, g_keepPitch ? "eb" : "75");
}

static void ApplyFilter()
{
    if (g_hasFilter) Hex(kFilter, g_noFilter ? "e98866000090" : "0f8787660000");
}

static void ApplyUi()
{
    if (!g_hasUi) return;
    bool on = g_ultrawide && ScreenAspect() > 1.79f;
    for (int i = 0; i < 2; i++) Hex(kUiScale[i], on ? "b80200000090" : g_uiOrig[i]);
}

static bool InEvent()
{
    BYTE* m = *(BYTE**)0x12340a4;
    if (!m) return false;
    BYTE* e = *(BYTE**)(m + 0x1037c);
    return e && e[0x268];
}

static bool __stdcall MouseTurn(BYTE* ctrl, BYTE* player)
{
    g_player = player;
    if (!g_freeCam) return false;
    if (*(DWORD*)(player + 0x10e0) > 8) return false;
    if (*(BYTE*)(player + 0x2920)) return false;
    DWORD f = *(DWORD*)(player + 0x2d8c);
    if (f & 0xC0) return false;
    if ((f & 0x400) && !(f & 0x800)) return false;
    float mx = *(float*)(ctrl + 0x6f4);
    BYTE* sm = *(BYTE**)0x1249c78;
    if (g_rawMouse && sm) mx = (float)*(int*)(sm + 0x2c);
    if (mx == 0.0f) return true;
    float d = -mx * g_sens * 0.002f;
    __declspec(align(16)) float a[4];
    GetAngle(player, a);
    a[1] += d;
    SetAngle(player, a);
    *(float*)(player + 0x2a04) += d;
    return true;
}

__declspec(naked) static void MouseStub()
{
    __asm {
        pushad
        pushfd
        fxsave g_fx
        push esi
        push ebx
        call MouseTurn
        mov g_res, al
        fxrstor g_fx
        popfd
        popad
        cmp g_res, 0
        jne skip
        movss xmm0, dword ptr [ebx + 0x6f4]
        jmp kMouseRet
    skip:
        jmp kMouseSkip
    }
}

static BYTE __stdcall QtePressed()
{
    bool down = (g_qteKey && (GetAsyncKeyState(g_qteKey) & 0x8000)) || (g_qteKey2 && (GetAsyncKeyState(g_qteKey2) & 0x8000));
    if (down)
    {
        if (g_armed) { g_armed = false; return 1; }
        return 0;
    }
    g_armed = true;
    return 0;
}

__declspec(naked) static void QteStubA()
{
    __asm {
        test al, al
        jnz done
        push ecx
        push edx
        call QtePressed
        pop edx
        pop ecx
    done:
        mov bl, al
        jmp kQteJoin
    }
}

__declspec(naked) static void QteStubB()
{
    __asm {
        test al, al
        jnz done
        push ecx
        push edx
        call QtePressed
        pop edx
        pop ecx
    done:
        mov bl, al
        mov eax, dword ptr [esi + 0xc]
        jmp kQteBRet
    }
}

static const char* kDotObjs[] = { "uOmExplosive", "uOmGlass", "uOmLock", "uOmChain", "uOmRoll", "uOmMotionDmg", "uOmCollapsePole", "uOmCoin", "uOmCartridge", "uItem" };

static bool DotObj(const char* n)
{
    if (!strncmp(n, "uEm", 3) || !strncmp(n, "uIt0", 4) || strstr(n, "Break")) return true;
    for (const char* k : kDotObjs) if (!strcmp(n, k)) return true;
    return false;
}

static BYTE __stdcall LaserTarget(BYTE* pl)
{
    typedef BYTE*(__thiscall* DtiFn)(void*);
    __try
    {
        if (*(DWORD*)(pl + 0x2790)) return 2;
        BYTE* c = *(BYTE**)(pl + 0x2890);
        if (!c || !c[0xbe] || !c[0xbc]) return 0;
        DWORD k = *(DWORD*)(c + 0xc4);
        if (k != 0 && k != 2) return 0;
        BYTE* t = *(BYTE**)(c + 0xc0);
        if (!t) return 0;
        BYTE* sh = *(BYTE**)(t + 0x3e4);
        if (!sh) return 0;
        void** o = *(void***)(sh + 0x90);
        if (!o) return 0;
        BYTE* d = ((DtiFn)(*(void***)o)[4])(o);
        for (int i = 0; d && i < 16; i++, d = *(BYTE**)(d + 0x10))
        {
            const char* n = *(const char**)(d + 4);
            if (n && DotObj(n)) return 1;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

__declspec(naked) static void AimStartStub()
{
    __asm {
        mov ecx, esi
        cmp g_levelAim, 0
        je keep
        xorps xmm0, xmm0
    keep:
        movss dword ptr [ebx + 0x408], xmm0
        jmp kAimStartRet
    }
}

__declspec(naked) static void LaserHitStub()
{
    __asm {
        mov byte ptr [edi + 0x2fa], bl
        mov g_laserVis, bl
        inc g_laserSeq
        pushad
        pushfd
        push esi
        call LaserTarget
        mov g_laserEnemy, al
        popfd
        popad
        jmp kLaserHitRet
    }
}

__declspec(naked) static void FovStub()
{
    __asm {
        push eax
        mov eax, dword ptr [ebp + 0x14]
        fld dword ptr [eax]
        fadd g_fovAdd
        fstp dword ptr [eax]
        pop eax
        movss xmm1, dword ptr [esi + 0x1d4]
        jmp kFovRet
    }
}

__declspec(naked) static void FpsStub()
{
    __asm {
        cmp g_fpsCap, 0
        jle orig
        divss xmm1, g_fpsCapF
        jmp done
    orig:
        divss xmm1, dword ptr [esi + 0x3c]
    done:
        movaps xmm0, xmm1
        jmp kFpsRet
    }
}

__declspec(naked) static void ShadowStub()
{
    __asm {
        cmp g_shadow, 0
        jle keep
        mov eax, g_shadow
    keep:
        add eax, 0xf
        push esi
        mov esi, ecx
        and eax, 0xfffffff0
        jmp kShadowRet
    }
}

static float FixAspect(float asp)
{
    if (!g_ultrawide || g_bbW <= 0 || g_bbH <= 0) return asp;
    float s = (float)g_bbW / (float)g_bbH;
    if (s > 1.79f && asp < s - 0.01f) return s;
    return asp;
}

static void* __fastcall PerspOther(void* m, void*, float n, float f, float fov, float asp)
{
    return Perspective(m, n, f, fov, FixAspect(asp));
}

static int __fastcall CatVol(BYTE* snd)
{
    DWORD c = *(DWORD*)(snd + 0x48);
    if (c > 4) return 0;
    BYTE* b = *(BYTE**)0x1249c60;
    int v = *(int*)(b + 0x3fdd0 + c * 4);
    float m = c == 1 ? g_music : (c == 0 || c == 2) ? g_sfx : 1.0f;
    if (g_cut != 1.0f && InEvent()) m *= g_cut;
    if (m == 1.0f) return v;
    return (int)(v * m + 0.5f);
}

static void PatchVt(void** vt, int idx, void* fn, void** orig)
{
    if (vt[idx] == fn) return;
    DWORD old;
    VirtualProtect(&vt[idx], 4, PAGE_EXECUTE_READWRITE, &old);
    if (orig) *orig = vt[idx];
    vt[idx] = fn;
    VirtualProtect(&vt[idx], 4, old, &old);
}

static const struct { const char* n; int vk; } kKeys[] = {
    { "SPACE", 0x20 }, { "ENTER", 0x0D }, { "TAB", 0x09 }, { "BACKSPACE", 0x08 }, { "CAPSLOCK", 0x14 },
    { "SHIFT", 0x10 }, { "CTRL", 0x11 }, { "ALT", 0x12 }, { "LSHIFT", 0xA0 }, { "RSHIFT", 0xA1 },
    { "LCTRL", 0xA2 }, { "RCTRL", 0xA3 }, { "LALT", 0xA4 }, { "RALT", 0xA5 },
    { "MOUSE3", 0x04 }, { "MOUSE4", 0x05 }, { "MOUSE5", 0x06 }, { "RMB", 0x02 },
    { "INSERT", 0x2D }, { "DELETE", 0x2E }, { "HOME", 0x24 }, { "END", 0x23 }, { "PGUP", 0x21 }, { "PGDN", 0x22 },
    { "UP", 0x26 }, { "DOWN", 0x28 }, { "LEFT", 0x25 }, { "RIGHT", 0x27 },
};

static const char* KeyName(int vk)
{
    static char b[8];
    if (!vk) return "None";
    for (auto& k : kKeys) if (k.vk == vk) return k.n;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { sprintf_s(b, "%c", vk); return b; }
    if (vk >= VK_F1 && vk <= VK_F24) { sprintf_s(b, "F%d", vk - VK_F1 + 1); return b; }
    sprintf_s(b, "0x%02X", vk);
    return b;
}

static void SaveIni()
{
    char b[32];
    sprintf_s(b, "%.3f", g_sens); WritePrivateProfileStringA("Mouse", "MouseSensitivity", b, g_ini);
    WritePrivateProfileStringA("Mouse", "FreeCameraWhileMoving", g_freeCam ? "1" : "0", g_ini);
    WritePrivateProfileStringA("Mouse", "RawMouse", g_rawMouse ? "1" : "0", g_ini);
    WritePrivateProfileStringA("Mouse", "RawAim", g_rawAim ? "1" : "0", g_ini);
    WritePrivateProfileStringA("QTE", "QTEKey", g_qteKey ? KeyName(g_qteKey) : "0", g_ini);
    WritePrivateProfileStringA("QTE", "QTEKey2", g_qteKey2 ? KeyName(g_qteKey2) : "0", g_ini);
    sprintf_s(b, "%.1f", g_fovAdd); WritePrivateProfileStringA("Camera", "FOVAdditional", b, g_ini);
    WritePrivateProfileStringA("Camera", "UltrawideFix", g_ultrawide ? "1" : "0", g_ini);
    WritePrivateProfileStringA("Camera", "KeepPitchWhileRunning", g_keepPitch ? "1" : "0", g_ini);
    WritePrivateProfileStringA("Camera", "LevelAimStart", g_levelAim ? "1" : "0", g_ini);
    WritePrivateProfileStringA("Display", "Borderless", g_borderless ? "1" : "0", g_ini);
    WritePrivateProfileStringA("Display", "DisableVsync", g_noVsync ? "1" : "0", g_ini);
    WritePrivateProfileStringA("Display", "FixDPIScale", g_dpi ? "1" : "0", g_ini);
    WritePrivateProfileStringA("Weapon", "LaserSight", g_laser ? "1" : "0", g_ini);
    sprintf_s(b, "%.1f", g_dotSize); WritePrivateProfileStringA("Weapon", "LaserDotSize", b, g_ini);
    sprintf_s(b, "%.1f", g_dotEnemy); WritePrivateProfileStringA("Weapon", "LaserDotSizeEnemy", b, g_ini);
    sprintf_s(b, "%.2f", g_music); WritePrivateProfileStringA("Audio", "MusicVolume", b, g_ini);
    sprintf_s(b, "%.2f", g_sfx); WritePrivateProfileStringA("Audio", "SFXVolume", b, g_ini);
    sprintf_s(b, "%.2f", g_cut); WritePrivateProfileStringA("Audio", "CutsceneVolume", b, g_ini);
    WritePrivateProfileStringA("Display", "RemoveColorFilter", g_noFilter ? "1" : "0", g_ini);
    WritePrivateProfileStringA("Display", "RemoveResolutionLimit", g_resLimit ? "1" : "0", g_ini);
    sprintf_s(b, "%d", g_fpsCap); WritePrivateProfileStringA("Display", "FPSCap", b, g_ini);
    sprintf_s(b, "%d", g_shadow); WritePrivateProfileStringA("Display", "ShadowResolution", b, g_ini);
    WritePrivateProfileStringA("Misc", "SkipIntroLogos", g_skipLogos ? "1" : "0", g_ini);
    WritePrivateProfileStringA("Overlay", "ToggleKey", KeyName(g_toggleKey), g_ini);
}

static void KeyInput(const char* label, int* key)
{
    char b[64];
    sprintf_s(b, "%s##%s", g_bind == key ? "Press a key (Esc = none)" : KeyName(*key), label);
    if (ImGui::Button(b, ImVec2(200, 0))) g_bind = key;
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
}

static void PollBind()
{
    if (!g_bind) return;
    for (int vk = 2; vk < 0xFF; vk++)
    {
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;
        if (!(GetAsyncKeyState(vk) & 0x8000)) continue;
        *g_bind = vk == VK_ESCAPE ? 0 : vk;
        g_bind = nullptr;
        return;
    }
}

static void DrawMenu()
{
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("RE5 Tweaks 1.0.14", &g_menu);
    if (ImGui::CollapsingHeader("Mouse", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Sensitivity", &g_sens, 0.1f, 5.0f, "%.2f");
        ImGui::Checkbox("Free camera while moving", &g_freeCam);
        ImGui::Checkbox("Raw mouse (turning)", &g_rawMouse);
        if (ImGui::Checkbox("Raw aim", &g_rawAim)) ApplyRawAim();
    }
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("FOV additional", &g_fovAdd, -20.0f, 60.0f, "%.0f deg");
        if (ImGui::Checkbox("Ultrawide fix (3D + HUD)", &g_ultrawide)) ApplyUi();
        if (ImGui::Checkbox("Keep camera height while running", &g_keepPitch)) ApplyPitch();
        ImGui::Checkbox("Aim starts level", &g_levelAim);
    }
    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Borderless", &g_borderless);
        ImGui::Checkbox("Disable VSync (next reset)", &g_noVsync);
        ImGui::Checkbox("Fix DPI scale (restart)", &g_dpi);
        if (ImGui::Checkbox("Remove green colour filter", &g_noFilter)) ApplyFilter();
        ImGui::Checkbox("Remove resolution limit (restart)", &g_resLimit);
        if (ImGui::InputInt("FPS cap (0 = game)", &g_fpsCap, 10, 30)) { if (g_fpsCap < 0) g_fpsCap = 0; g_fpsCapF = (float)g_fpsCap; }
        if (ImGui::InputInt("Shadow res (0 = game, reload)", &g_shadow, 256, 1024)) { if (g_shadow < 0) g_shadow = 0; if (g_shadow > 8192) g_shadow = 8192; }
    }
    if (ImGui::CollapsingHeader("Weapon", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Laser sight from muzzle", &g_laser)) ApplyLaser();
        ImGui::SliderFloat("Dot size on objects", &g_dotSize, 0.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("Dot size on enemy", &g_dotEnemy, 0.0f, 20.0f, "%.1f");
    }
    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Music", &g_music, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("SFX", &g_sfx, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Cutscene", &g_cut, 0.0f, 1.0f, "%.2f");
    }
    if (ImGui::CollapsingHeader("Keys", ImGuiTreeNodeFlags_DefaultOpen))
    {
        PollBind();
        KeyInput("QTE key", &g_qteKey);
        KeyInput("QTE key 2", &g_qteKey2);
        KeyInput("Overlay key", &g_toggleKey);
        ImGui::Checkbox("Skip intro logos (restart)", &g_skipLogos);
    }
    if (ImGui::Button("Save")) SaveIni();
    ImGui::End();
}

static LRESULT CALLBACK HookWndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (g_imgui && g_menu)
    {
        EnterCriticalSection(&g_cs);
        ImGui_ImplWin32_WndProcHandler(h, msg, w, l);
        LeaveCriticalSection(&g_cs);
        if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || msg == WM_INPUT) return 0;
    }
    return CallWindowProcA(g_wndProc, h, msg, w, l);
}

typedef HRESULT(__stdcall* PresentFn)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(__stdcall* ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT(__stdcall* CreateDeviceFn)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT(__stdcall* DIStateFn)(IDirectInputDevice8A*, DWORD, LPVOID);
typedef HRESULT(__stdcall* DIDataFn)(IDirectInputDevice8A*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
static PresentFn oPresent;
static ResetFn oReset;
static CreateDeviceFn oCreateDevice;
static DIStateFn oGetState;
static DIDataFn oGetData;

static void FixParams(D3DPRESENT_PARAMETERS* pp, HWND focus)
{
    if (g_noVsync) pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    if (g_borderless && !pp->Windowed)
    {
        pp->Windowed = TRUE;
        pp->FullScreen_RefreshRateInHz = 0;
    }
    g_bbW = pp->BackBufferWidth;
    g_bbH = pp->BackBufferHeight;
    HWND w = pp->hDeviceWindow ? pp->hDeviceWindow : focus;
    if ((!g_bbW || !g_bbH) && w)
    {
        RECT r;
        GetClientRect(w, &r);
        g_bbW = r.right;
        g_bbH = r.bottom;
    }
}

static void DrawDot()
{
    DWORD now = GetTickCount();
    if (g_laserSeq != g_laserLast) { g_laserLast = g_laserSeq; g_laserSeen = now; }
    if (!g_laser || !g_laserVis || now - g_laserSeen > 100 || InEvent()) return;
    if (!g_laserEnemy) return;
    float r = (g_laserEnemy == 2 ? g_dotEnemy : g_dotSize) * ImGui::GetIO().DisplaySize.y / 1080.0f;
    if (r <= 0.0f) return;
    ImVec2 c(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddCircleFilled(c, r * 2.5f, IM_COL32(255, 30, 30, 40), 24);
    dl->AddCircleFilled(c, r * 1.6f, IM_COL32(255, 30, 30, 90), 24);
    dl->AddCircleFilled(c, r, IM_COL32(255, 60, 60, 255), 16);
    dl->AddCircleFilled(c, r * 0.45f, IM_COL32(255, 200, 200, 255), 12);
}

static HRESULT __stdcall HookPresent(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND c, const RGNDATA* d)
{
    bool down = (GetAsyncKeyState(g_toggleKey) & 0x8000) != 0;
    if (down && !g_toggleDown) g_menu = !g_menu;
    g_toggleDown = down;
    if (!g_imgui && g_hwnd)
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui_ImplWin32_Init(g_hwnd);
        ImGui_ImplDX9_Init(dev);
        g_wndProc = (WNDPROC)SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)HookWndProc);
        g_imgui = true;
    }
    bool dot = g_laser && g_laserVis && GetTickCount() - g_laserSeen <= 100;
    if (g_imgui && (g_menu || dot || g_laserSeq != g_laserLast))
    {
        EnterCriticalSection(&g_cs);
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::GetIO().MouseDrawCursor = g_menu;
        ImGui::NewFrame();
        DrawDot();
        if (g_menu) DrawMenu();
        ImGui::EndFrame();
        IDirect3DSurface9* old = nullptr;
        IDirect3DSurface9* bb = nullptr;
        dev->GetRenderTarget(0, &old);
        if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
        {
            dev->SetRenderTarget(0, bb);
            if (SUCCEEDED(dev->BeginScene()))
            {
                ImGui::Render();
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
                dev->EndScene();
            }
            dev->SetRenderTarget(0, old);
            bb->Release();
        }
        if (old) old->Release();
        LeaveCriticalSection(&g_cs);
    }
    return oPresent(dev, a, b, c, d);
}

static HRESULT __stdcall HookReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
    if (g_imgui) ImGui_ImplDX9_InvalidateDeviceObjects();
    FixParams(pp, g_hwnd);
    HRESULT hr = oReset(dev, pp);
    if (g_imgui && SUCCEEDED(hr)) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

static HRESULT __stdcall HookCreateDevice(IDirect3D9* d3d, UINT ad, D3DDEVTYPE t, HWND focus, DWORD fl, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
{
    FixParams(pp, focus);
    HRESULT hr = oCreateDevice(d3d, ad, t, focus, fl, pp, out);
    if (SUCCEEDED(hr) && out && *out)
    {
        g_hwnd = pp->hDeviceWindow ? pp->hDeviceWindow : focus;
        void** vt = *(void***)*out;
        PatchVt(vt, 16, HookReset, (void**)&oReset);
        PatchVt(vt, 17, HookPresent, (void**)&oPresent);
    }
    return hr;
}

static HRESULT __stdcall HookGetState(IDirectInputDevice8A* dev, DWORD cb, LPVOID data)
{
    HRESULT hr = oGetState(dev, cb, data);
    if (g_menu && SUCCEEDED(hr) && data) memset(data, 0, cb);
    return hr;
}

static HRESULT __stdcall HookGetData(IDirectInputDevice8A* dev, DWORD cb, LPDIDEVICEOBJECTDATA buf, LPDWORD n, DWORD fl)
{
    HRESULT hr = oGetData(dev, cb, buf, n, fl);
    if (g_menu && SUCCEEDED(hr) && n) *n = 0;
    return hr;
}

static void HookD3D()
{
    HMODULE m = LoadLibraryA("d3d9.dll");
    typedef IDirect3D9*(WINAPI* CreateFn)(UINT);
    CreateFn create = m ? (CreateFn)GetProcAddress(m, "Direct3DCreate9") : nullptr;
    IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!d3d) return;
    PatchVt(*(void***)d3d, 16, HookCreateDevice, (void**)&oCreateDevice);
    d3d->Release();
}

static void HookDInput()
{
    HMODULE m = LoadLibraryA("dinput8.dll");
    typedef HRESULT(WINAPI* CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    CreateFn create = m ? (CreateFn)GetProcAddress(m, "DirectInput8Create") : nullptr;
    IDirectInput8A* di = nullptr;
    if (!create || FAILED(create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A, (LPVOID*)&di, nullptr)) || !di) return;
    IDirectInputDevice8A* dev = nullptr;
    if (SUCCEEDED(di->CreateDevice(GUID_SysKeyboard, &dev, nullptr)) && dev)
    {
        void** vt = *(void***)dev;
        PatchVt(vt, 9, HookGetState, (void**)&oGetState);
        PatchVt(vt, 10, HookGetData, (void**)&oGetData);
        dev->Release();
    }
    di->Release();
}

static BOOL CALLBACK FindWnd(HWND w, LPARAM l)
{
    DWORD pid;
    GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(w) && !GetWindow(w, GW_OWNER))
    {
        RECT r;
        GetClientRect(w, &r);
        if (r.right > 200) { *(HWND*)l = w; return FALSE; }
    }
    return TRUE;
}

static DWORD WINAPI BorderlessLoop(LPVOID)
{
    for (;;)
    {
        Sleep(1000);
        if (!g_borderless) continue;
        HWND w = g_hwnd;
        if (!w) EnumWindows(FindWnd, (LPARAM)&w);
        if (!w) continue;
        LONG st = GetWindowLongA(w, GWL_STYLE);
        if (!(st & WS_CAPTION)) continue;
        HMONITOR m = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoA(m, &mi);
        SetWindowLongA(w, GWL_STYLE, (st & ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME)) | WS_POPUP);
        SetWindowLongA(w, GWL_EXSTYLE, GetWindowLongA(w, GWL_EXSTYLE) & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
        SetWindowPos(w, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

static float IniFloat(const char* sec, const char* key, const char* def)
{
    char buf[64];
    GetPrivateProfileStringA(sec, key, def, buf, sizeof(buf), g_ini);
    return (float)atof(buf);
}

static int IniKey(const char* sec, const char* key)
{
    char buf[64];
    GetPrivateProfileStringA(sec, key, "0", buf, sizeof(buf), g_ini);
    char* e = buf + strlen(buf);
    while (e > buf && (e[-1] == ' ' || e[-1] == '	')) *--e = 0;
    _strupr_s(buf);
    for (auto& k : kKeys) if (!strcmp(buf, k.n)) return k.vk;
    if (buf[0] && !buf[1] && ((buf[0] >= 'A' && buf[0] <= 'Z') || (buf[0] >= '1' && buf[0] <= '9'))) return buf[0];
    if (buf[0] == 'F' && buf[1] >= '1' && buf[1] <= '9') { int n = atoi(buf + 1); if (n >= 1 && n <= 24) return VK_F1 + n - 1; }
    return (int)strtol(buf, nullptr, 0) & 0xFF;
}

static bool IniBool(const char* sec, const char* key, int def)
{
    return GetPrivateProfileIntA(sec, key, def, g_ini) != 0;
}

static float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

static void LoadIni(HMODULE h)
{
    GetModuleFileNameA(h, g_ini, MAX_PATH);
    char* s = strrchr(g_ini, '\\');
    strcpy_s(s + 1, MAX_PATH - (s + 1 - g_ini), "re5tweaks.ini");
    g_sens = IniFloat("Mouse", "MouseSensitivity", "1.0");
    g_freeCam = IniBool("Mouse", "FreeCameraWhileMoving", 1);
    g_rawMouse = IniBool("Mouse", "RawMouse", 1);
    g_rawAim = IniBool("Mouse", "RawAim", 1);
    g_qteKey = IniKey("QTE", "QTEKey");
    g_qteKey2 = IniKey("QTE", "QTEKey2");
    g_fovAdd = IniFloat("Camera", "FOVAdditional", "0");
    g_ultrawide = IniBool("Camera", "UltrawideFix", 1);
    g_keepPitch = IniBool("Camera", "KeepPitchWhileRunning", 1);
    g_levelAim = IniBool("Camera", "LevelAimStart", 1);
    g_borderless = IniBool("Display", "Borderless", 0);
    g_noVsync = IniBool("Display", "DisableVsync", 0);
    g_dpi = IniBool("Display", "FixDPIScale", 1);
    g_laser = IniBool("Weapon", "LaserSight", 0);
    g_dotSize = IniFloat("Weapon", "LaserDotSize", "4");
    g_dotEnemy = IniFloat("Weapon", "LaserDotSizeEnemy", "7");
    g_music = Clamp01(IniFloat("Audio", "MusicVolume", "1.0"));
    g_sfx = Clamp01(IniFloat("Audio", "SFXVolume", "1.0"));
    g_cut = Clamp01(IniFloat("Audio", "CutsceneVolume", "1.0"));
    g_skipLogos = IniBool("Misc", "SkipIntroLogos", 1);
    g_noFilter = IniBool("Display", "RemoveColorFilter", 0);
    g_resLimit = IniBool("Display", "RemoveResolutionLimit", 1);
    g_fpsCap = GetPrivateProfileIntA("Display", "FPSCap", 0, g_ini);
    g_fpsCapF = (float)g_fpsCap;
    g_shadow = GetPrivateProfileIntA("Display", "ShadowResolution", 0, g_ini);
    int k = IniKey("Overlay", "ToggleKey");
    if (k) g_toggleKey = k;
}

static DWORD WINAPI Init(LPVOID)
{
    HookD3D();
    HookDInput();
    for (int i = 0; i < 600 && !Match(kMouseSite, "f30f1083f4060000"); i++) Sleep(100);
    if (g_skipLogos)
    {
        if (Match(kLogoBoot, "8b0d409c2401")) Jmp(kLogoBoot, (void*)kLogoBootSkip, 6);
        if (Match(kLogoMovie, "f7d8894610")) Hex(kLogoMovie, "834e10ff90");
    }
    if (Match(kMouseSite, "f30f1083f4060000")) Jmp(kMouseSite, MouseStub, 8);
    g_hasRawAim = Match(kRawAim, "760a");
    ApplyRawAim();
    g_hasLaser = Match(kLaserLine, "32db") && Match(kLaserStart, "0f84db010000");
    ApplyLaser();
    if (g_hasLaser && Match(kLaserHit, "889ffa020000")) Jmp(kLaserHit, LaserHitStub, 6);
    if (Match(kQteB, "8ad88b460c"))
    {
        bool ok = true;
        for (DWORD a : kQteA) ok = ok && Match(a, "8ad8e9");
        if (ok)
        {
            for (DWORD a : kQteA) Jmp(a, QteStubA, 7);
            Jmp(kQteB, QteStubB, 5);
        }
    }
    if (IsCall(kPerspGame, (DWORD)Perspective)) CallRel(kPerspGame, PerspOther);
    for (DWORD a : kPerspOther)
        if (IsCall(a, (DWORD)Perspective)) CallRel(a, PerspOther);
    if (Match(kCatVol, "8b494833c083f904")) Jmp(kCatVol, CatVol, 5);
    if (Match(kFov, "f30f108ed4010000")) Jmp(kFov, FovStub, 8);
    if (Match(kFps, "f30f5e4e3c0f28c1")) Jmp(kFps, FpsStub, 8);
    if (Match(kShadow, "83c00f568bf183e0f0")) Jmp(kShadow, ShadowStub, 9);
    g_hasFilter = Match(kFilter, "0f8787660000");
    ApplyFilter();
    g_hasPitch = Match(kRunRecenter, "7536");
    ApplyPitch();
    if (Match(kAimStart, "8bcef30f118308040000")) Jmp(kAimStart, AimStartStub, 10);
    g_hasUi = Match(kUiScale[0], "8b83e04e0000") && Match(kUiScale[1], "8b81e04e0000");
    strcpy_s(g_uiOrig[0], "8b83e04e0000");
    strcpy_s(g_uiOrig[1], "8b81e04e0000");
    ApplyUi();
    if (g_resLimit)
    {
        for (int i = 0; i < 300 && !*(BYTE**)0x12337dc; i++) Sleep(100);
        BYTE* r = *(BYTE**)0x12337dc;
        if (r)
        {
            *(DWORD*)(r + 0x50) = 0x20000;
            *(DWORD*)(r + 0x54) = 0x20000;
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID)
{
    if (r == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&g_cs);
        LoadIni(h);
        if (g_dpi) SetProcessDPIAware();
        CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, BorderlessLoop, nullptr, 0, nullptr);
    }
    return TRUE;
}
