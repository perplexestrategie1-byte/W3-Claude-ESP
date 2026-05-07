#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <iostream>
#include <vector>
#include <iomanip>
#include <psapi.h>

// W3 Claude Project - Phase 48 : Ghost Filter & Live Telemetry
// Objectif : Remettre la console pour diagnostiquer la Matrice (verifie si elle freeze)
// et ajouter un filtre de "Handle" pour ignorer les unites mortes/anciennes (Ghosts) dans la memoire.

uintptr_t War3Base = 0;
HWND hwndW3 = NULL;
HWND hwndOverlay = NULL;
int g_Width = 1920;
int g_Height = 1080;

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct Matrix4x4 { float m[16]; };

bool ReadMemory(uintptr_t address, void* buffer, size_t size) {
    SIZE_T bytesRead;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, buffer, size, &bytesRead) && bytesRead == size;
}

uintptr_t ReadPtr(uintptr_t address) {
    uintptr_t ptr = 0;
    ReadMemory(address, &ptr, sizeof(ptr));
    return ptr;
}

void InitModuleInfo() {
    if (War3Base == 0) {
        HMODULE hMod = GetModuleHandleA("Warcraft III.exe");
        if (hMod) War3Base = (uintptr_t)hMod;

        hwndW3 = FindWindowA("GxWindowClass", "Warcraft III");
        if (!hwndW3) hwndW3 = FindWindowA(NULL, "Warcraft III");
    }
}

bool WorldToScreen(Vec3 world, Vec2* screen, Matrix4x4 mat) {
    Vec4 clip;
    clip.x = world.x * mat.m[0] + world.y * mat.m[4] + world.z * mat.m[8] + mat.m[12];
    clip.y = world.x * mat.m[1] + world.y * mat.m[5] + world.z * mat.m[9] + mat.m[13];
    clip.z = world.x * mat.m[2] + world.y * mat.m[6] + world.z * mat.m[10] + mat.m[14];
    clip.w = world.x * mat.m[3] + world.y * mat.m[7] + world.z * mat.m[11] + mat.m[15];

    if (clip.w < 0.1f) return false; // Derriere la camera

    Vec3 NDC;
    NDC.x = clip.x / clip.w;
    NDC.y = clip.y / clip.w;

    screen->x = (g_Width / 2.0f) * (1.0f + NDC.x);
    screen->y = (g_Height / 2.0f) * (1.0f - NDC.y);

    return true;
}

bool IsValidCoordinate(float val) {
    return (val > -15000.0f && val < 15000.0f && (val > 1.0f || val < -1.0f));
}

// Thread Console pour la Telemetrie (Debug)
DWORD WINAPI TelemetryThread(LPVOID lpParam) {
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);

    std::cout << "--- W3 CLAUDE - STRATEGIE HYBRIDE V37 (PHASE 48) ---" << std::endl;
    std::cout << "Outil: Telemetrie Live & Ghost Filter" << std::endl;

    while (true) {
        InitModuleInfo();
        if (War3Base != 0) {
            uintptr_t camPtr = ReadPtr(War3Base + 0xd94de28);
            Matrix4x4 mat;
            if (camPtr > 0x10000000 && ReadMemory(camPtr + 0x158, &mat, sizeof(mat))) {
                std::cout << "\r[Live] Matrice m[0]: " << std::fixed << std::setprecision(4) << mat.m[0]
                    << " | m[12] (TransX): " << mat.m[12] << "      ";
            }
        }
        Sleep(500); // Update console 2 fois par seconde
    }
    return 0;
}

// Fonction de rendu (Overlay)
void RenderESP(HDC hdc) {
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, g_Width, g_Height);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

    HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
    RECT bgRect = { 0, 0, g_Width, g_Height };
    FillRect(hdcMem, &bgRect, bgBrush);
    DeleteObject(bgBrush);

    InitModuleInfo();
    if (War3Base != 0) {
        uintptr_t camPtr = ReadPtr(War3Base + 0xd94de28);
        Matrix4x4 mat;
        bool hasMatrix = false;
        if (camPtr > 0x10000000 && ReadMemory(camPtr + 0x158, &mat, sizeof(mat))) {
            hasMatrix = true;
        }

        if (hasMatrix) {
            uintptr_t level1 = ReadPtr(War3Base + 0xd94da78);
            if (level1 > 0x10000000) {
                HPEN boxPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
                HGDIOBJ oldPen = SelectObject(hdcMem, boxPen);
                HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
                HGDIOBJ oldBrush = SelectObject(hdcMem, nullBrush);

                for (int chunkOffset = 0x3b0; chunkOffset <= 0x800; chunkOffset += 0x38) {
                    uintptr_t chunkPtr = ReadPtr(level1 + chunkOffset);
                    if (chunkPtr > 0x10000000 && chunkPtr < 0x7FFFFFFFFFFF) {
                        std::vector<byte> buffer(0x4000);
                        if (ReadMemory(chunkPtr, buffer.data(), 0x4000)) {
                            // Parcours du Chunk
                            for (size_t i = 0; i < 0x4000 - 20; i += 4) {
                                float x = *(float*)&buffer[i];
                                float y = *(float*)&buffer[i + 16];
                                uint32_t handle = *(uint32_t*)&buffer[i + 4]; // Le fameux Entity Handle

                                // FILTRE GHOST : Un vrai Handle W3 Reforged ressemble a 0x1334XXXX ou est > 0x01000000
                                // Les entites mortes (supprimees de la partie) gardent leurs coordonnees mais leur Handle est souvent zero ou invalide.
                                if (IsValidCoordinate(x) && IsValidCoordinate(y) && handle > 0x01000000) {
                                    Vec3 worldPos = { x, y, 0.0f };
                                    Vec2 screenPos;

                                    if (WorldToScreen(worldPos, &screenPos, mat)) {
                                        if (screenPos.x > 0 && screenPos.x < g_Width && screenPos.y > 0 && screenPos.y < g_Height) {
                                            int boxSize = 15;
                                            Rectangle(hdcMem, (int)screenPos.x - boxSize, (int)screenPos.y - boxSize,
                                                (int)screenPos.x + boxSize, (int)screenPos.y + boxSize);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                SelectObject(hdcMem, oldPen);
                SelectObject(hdcMem, oldBrush);
                DeleteObject(boxPen);
            }
        }
    }

    BitBlt(hdc, 0, 0, g_Width, g_Height, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RenderESP(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

DWORD WINAPI OverlayThread(LPVOID lpParam) {
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), 0, OverlayProc, 0, 0, NULL, NULL, NULL, NULL, NULL, "W3ClaudeOverlay", NULL };
    RegisterClassExA(&wc);

    InitModuleInfo();
    RECT rect;
    if (hwndW3 && GetClientRect(hwndW3, &rect)) {
        POINT pt = { 0, 0 };
        ClientToScreen(hwndW3, &pt);
        g_Width = rect.right - rect.left;
        g_Height = rect.bottom - rect.top;

        hwndOverlay = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
            "W3ClaudeOverlay", "ClaudeESP",
            WS_POPUP,
            pt.x, pt.y, g_Width, g_Height,
            NULL, NULL, NULL, NULL
        );

        SetLayeredWindowAttributes(hwndOverlay, RGB(0, 0, 0), 255, LWA_COLORKEY);
        ShowWindow(hwndOverlay, SW_SHOW);

        MSG msg;
        while (true) {
            if (hwndW3 && GetClientRect(hwndW3, &rect)) {
                POINT p = { 0, 0 };
                ClientToScreen(hwndW3, &p);
                g_Width = rect.right - rect.left;
                g_Height = rect.bottom - rect.top;
                MoveWindow(hwndOverlay, p.x, p.y, g_Width, g_Height, true);
            }

            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            InvalidateRect(hwndOverlay, NULL, FALSE);
            Sleep(16);
        }
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(0, 0, TelemetryThread, hModule, 0, 0);
        CreateThread(0, 0, OverlayThread, hModule, 0, 0);
    }
    return TRUE;
}
