#include <windows.h>
#include <iostream>
#include <string>
#include <tlhelp32.h>

// [W3 Claude Project] - Injector Fixed

DWORD GetProcessIdByName(const std::wstring& processName) {
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            if (processName == pe32.szExeFile) {
                CloseHandle(hSnapshot);
                return pe32.th32ProcessID;
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return 0;
}

int main() {
    // UPDATED TARGET PROCESS NAME
    std::wstring targetProcess = L"Warcraft III.exe";
    std::wstring dllName = L"W3_Payload.dll";

    // 1. Get current directory for absolute DLL path
    wchar_t currentDir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, currentDir);
    // Note: the original generated code had an issue with path formatting. Using single slash.
    std::wstring dllPath = std::wstring(currentDir) + L"\\" + dllName;

    std::wcout << L"[*] W3 Injector Initialized." << std::endl;
    std::wcout << L"[*] Target: " << targetProcess << std::endl;
    std::wcout << L"[*] DLL: " << dllPath << std::endl;

    // 2. Find Process
    DWORD pid = GetProcessIdByName(targetProcess);
    if (pid == 0) {
        std::wcerr << L"[-] Process not found. Is the game running?" << std::endl;
        system("pause");
        return 1;
    }
    std::wcout << L"[+] PID found: " << pid << std::endl;

    // 3. Inject (Standard LoadLibrary for initial testing)
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        std::wcerr << L"[-] Failed to open process. Run as Admin." << std::endl;
        system("pause");
        return 1;
    }

    void* loc = VirtualAllocEx(hProcess, 0, (dllPath.length() + 1) * sizeof(wchar_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!loc) {
        std::wcerr << L"[-] VirtualAllocEx failed." << std::endl;
        CloseHandle(hProcess);
        system("pause");
        return 1;
    }

    WriteProcessMemory(hProcess, loc, dllPath.c_str(), (dllPath.length() + 1) * sizeof(wchar_t), 0);

    HANDLE hThread = CreateRemoteThread(hProcess, 0, 0, (LPTHREAD_START_ROUTINE)LoadLibraryW, loc, 0, 0);
    if (hThread) {
        std::wcout << L"[+] Injection successful. Waiting for thread..." << std::endl;
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }
    else {
        std::wcerr << L"[-] CreateRemoteThread failed." << std::endl;
    }

    VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    std::wcout << L"[*] Injector shutting down." << std::endl;
    system("pause");
    return 0;
}
