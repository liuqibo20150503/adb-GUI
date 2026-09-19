// adb_gui_tool.cpp  (v3.2 中文版 - 修复重启/fastboot + scrcpy)
// 编译: g++ -o adb_gui.exe adb_gui_tool.cpp -mwindows -lcomctl32 -lshlwapi -static -std=c++17 -O2

#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>
#include <map>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

// ============ 主题色 ============
#define COL_BG          RGB(30, 30, 30)
#define COL_PANEL       RGB(37, 37, 38)
#define COL_PANEL2      RGB(45, 45, 48)
#define COL_BORDER      RGB(60, 60, 60)
#define COL_TEXT        RGB(220, 220, 220)
#define COL_TEXT_DIM    RGB(150, 150, 150)
#define COL_ACCENT      RGB(0, 122, 204)
#define COL_ACCENT_HOV  RGB(28, 151, 234)
#define COL_ACCENT_PRS  RGB(0, 90, 158)
#define COL_INPUT_BG    RGB(45, 45, 48)      // ★ 输入框背景
#define COL_SUCCESS     RGB(78, 201, 176)
#define COL_ERROR       RGB(244, 135, 113)
#define COL_CMD         RGB(86, 156, 214)
#define COL_LIST_SEL    RGB(0, 122, 204)

// ============ 自定义消息 ============
#define WM_APPEND_OUTPUT   (WM_APP + 1)
#define WM_APPEND_TERM     (WM_APP + 2)
#define WM_DEVICES_READY   (WM_APP + 100)
#define WM_TERM_DEAD       (WM_APP + 102)

// ============ 控件ID ============
#define IDC_TAB             900
#define IDC_STATUS          901
#define IDC_DEVICE_LIST     1001
#define IDC_REFRESH_BTN     1002
#define IDC_OUTPUT_EDIT     1003
#define IDC_COMMAND_EDIT    1004
#define IDC_EXECUTE_BTN     1005
#define IDC_INSTALL_BTN     1006
#define IDC_SCREENSHOT_BTN  1007
#define IDC_REBOOT_BTN      1008
#define IDC_SHELL_BTN       1009
#define IDC_LOGCAT_BTN      1012
#define IDC_CLEAR_BTN       1013
#define IDC_WIRELESS_BTN    1014
#define IDC_FILES_BTN       1015
#define IDC_PROCS_BTN       1016
#define IDC_APPS_BTN        1017
#define IDC_INFO_BTN        1018
#define IDC_REBOOT_RECOV    1020
#define IDC_REBOOT_BOOT     1021
#define IDC_BATTERY_BTN     1022
#define IDC_SCRCPY_BTN      1025
#define IDC_SCRCPY_SET      1026
#define IDC_TERM_OUTPUT     2001
#define IDC_TERM_INPUT      2002
#define IDC_TERM_SEND       2003
#define IDC_TERM_CLEAR      2004
#define IDC_TERM_RESTART    2005

// ============ 全局 ============
HWND g_hMainWnd = NULL;
HWND g_hTab = NULL;
HWND g_hStatus = NULL;

HWND g_hDeviceList = NULL;
HWND g_hOutputEdit = NULL;
HWND g_hCommandEdit = NULL;
HWND g_hShellBtn = NULL;
HWND g_hLogcatBtn = NULL;
std::vector<HWND> g_adbPageCtrls;

HWND g_hTermOutput = NULL;
HWND g_hTermInput = NULL;
HWND g_hTermCtrls[5] = {0};

HFONT g_hFontUI = NULL;
HFONT g_hFontMono = NULL;
HFONT g_hFontBold = NULL;

HBRUSH g_brBg = NULL;
HBRUSH g_brPanel = NULL;
HBRUSH g_brInput = NULL;

// scrcpy
std::string g_scrcpyPath;
std::string g_iniPath;
PROCESS_INFORMATION g_scrcpyPi{0};
std::atomic<bool> g_scrcpyRunning{false};

// Shell / Term
struct ShellSession {
    HANDLE hProcess = NULL;
    HANDLE hStdIn = NULL;
    HANDLE hStdOut = NULL;
    std::thread reader;
    std::atomic<bool> running{false};
    std::mutex writeMtx;
} g_shell;

struct TermSession {
    HANDLE hProcess = NULL;
    HANDLE hStdIn = NULL;
    HANDLE hStdOut = NULL;
    std::thread reader;
    std::atomic<bool> running{false};
    std::mutex writeMtx;
} g_term;

std::atomic<bool> g_logcatRunning{false};
HANDLE g_logcatProc = NULL;
HANDLE g_logcatOut = NULL;
std::thread g_logcatReader;

// 按钮自绘
struct BtnStyle { bool accent; bool small; };
std::map<HWND, BtnStyle> g_btnStyles;
std::map<HWND, int> g_btnHover;

// =========================================================
// 编码
// =========================================================
std::wstring U8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
std::string WToU8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}
std::string U8ToAnsi(const std::string& s) {
    if (s.empty()) return "";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (wlen <= 0) return s;
    std::wstring w(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], wlen);
    int alen = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(),
                                   NULL, 0, NULL, NULL);
    if (alen <= 0) return s;
    std::string a(alen, 0);
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(),
                        &a[0], alen, NULL, NULL);
    return a;
}
bool IsAscii(const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return false;
    return true;
}

// =========================================================
// 输出
// =========================================================
void PostOutput(const std::string& text) {
    if (!g_hMainWnd) return;
    char* p = new char[text.size() + 1];
    memcpy(p, text.c_str(), text.size() + 1);
    PostMessage(g_hMainWnd, WM_APPEND_OUTPUT, (WPARAM)p, 0);
}
void PostTerm(const std::string& text) {
    if (!g_hMainWnd) return;
    char* p = new char[text.size() + 1];
    memcpy(p, text.c_str(), text.size() + 1);
    PostMessage(g_hMainWnd, WM_APPEND_TERM, (WPARAM)p, 0);
}
void SetStatus(const std::wstring& text) {
    if (g_hStatus) SetWindowTextW(g_hStatus, text.c_str());
}
void DoAppendOutput(const std::string& text) {
    if (!g_hOutputEdit) return;
    std::wstring w = U8ToW(text);
    int len = GetWindowTextLengthW(g_hOutputEdit);
    SendMessageW(g_hOutputEdit, EM_SETSEL, len, len);
    SendMessageW(g_hOutputEdit, EM_REPLACESEL, FALSE, (LPARAM)w.c_str());
    SendMessageW(g_hOutputEdit, EM_SCROLLCARET, 0, 0);
    if (len > 200000) {
        SendMessageW(g_hOutputEdit, EM_SETSEL, 0, len / 2);
        SendMessageW(g_hOutputEdit, EM_REPLACESEL, FALSE, (LPARAM)L"[...已截断...]\r\n");
    }
}
void DoAppendTerm(const std::string& text) {
    if (!g_hTermOutput) return;
    std::wstring w = U8ToW(text);
    int len = GetWindowTextLengthW(g_hTermOutput);
    SendMessageW(g_hTermOutput, EM_SETSEL, len, len);
    SendMessageW(g_hTermOutput, EM_REPLACESEL, FALSE, (LPARAM)w.c_str());
    SendMessageW(g_hTermOutput, EM_SCROLLCARET, 0, 0);
    if (len > 200000) {
        SendMessageW(g_hTermOutput, EM_SETSEL, 0, len / 2);
        SendMessageW(g_hTermOutput, EM_REPLACESEL, FALSE, (LPARAM)L"[...已截断...]\r\n");
    }
}

// =========================================================
// 一次性命令 (带超时)
// =========================================================
void RunStreaming(const std::string& cmdLineUtf8,
                  std::function<void(const std::string&)> onChunk,
                  std::function<void()> onDone = nullptr,
                  DWORD timeoutMs = 0)
{
    std::thread([cmdLineUtf8, onChunk, onDone, timeoutMs]() {
        SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
        HANDLE hRead = NULL, hWrite = NULL;
        if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
            if(onChunk) onChunk("CreatePipe 失败\n");
            if(onDone) onDone();
            return;
        }
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{ sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hWrite;
        si.hStdError  = hWrite;

        PROCESS_INFORMATION pi{};
        std::string fullUtf8 = "cmd.exe /c " + cmdLineUtf8;
        std::string fullAnsi = U8ToAnsi(fullUtf8);
        std::vector<char> cmdBuf(fullAnsi.begin(), fullAnsi.end());
        cmdBuf.push_back(0);

        if (!CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, TRUE,
                            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hRead); CloseHandle(hWrite);
            if(onChunk) onChunk("CreateProcess 失败\n");
            if(onDone) onDone();
            return;
        }
        CloseHandle(hWrite);

        char buf[4096];
        DWORD n = 0;
        DWORD t0 = GetTickCount();

        while (ReadFile(hRead, buf, sizeof(buf) - 1, &n, NULL) && n > 0) {
            buf[n] = 0;
            if (onChunk) onChunk(std::string(buf, n));
            if (timeoutMs > 0 && GetTickCount() - t0 > timeoutMs) {
                if (onChunk) onChunk("\n[超时, 强制结束命令]\n");
                break;
            }
        }
        CloseHandle(hRead);

        if (timeoutMs > 0) {
            DWORD wr = WaitForSingleObject(pi.hProcess, timeoutMs);
            if (wr == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 0);
        } else {
            WaitForSingleObject(pi.hProcess, INFINITE);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (onDone) onDone();
    }).detach();
}

void RunToOutput(const std::string& cmdLine) {
    PostOutput("\r\n");
    PostOutput("[CMD] " + cmdLine + "\r\n");
    RunStreaming(cmdLine, [](const std::string& s){ PostOutput(s); });
}

// =========================================================
// 设备
// =========================================================
std::string StripPrefix(const std::string& s) {
    if (!s.empty() && s[0] == '[') {
        size_t p = s.find("] ");
        if (p != std::string::npos) return s.substr(p + 2);
    }
    return s;
}
bool IsFastbootItem(const std::string& s) {
    return s.find("[fastboot]") == 0;
}

std::vector<std::string> GetSelectedDevices() {
    std::vector<std::string> result;
    if (!g_hDeviceList) return result;
    int count = SendMessage(g_hDeviceList, LB_GETSELCOUNT, 0, 0);
    if (count <= 0) return result;
    std::vector<int> idx(count);
    SendMessage(g_hDeviceList, LB_GETSELITEMS, count, (LPARAM)idx.data());
    for (int i : idx) {
        int len = SendMessage(g_hDeviceList, LB_GETTEXTLEN, i, 0);
        if (len <= 0) continue;
        std::wstring w(len + 1, 0);
        SendMessageW(g_hDeviceList, LB_GETTEXT, i, (LPARAM)&w[0]);
        w.resize(len);
        std::string s = WToU8(w);
        if (s == "(无设备)") continue;
        result.push_back(StripPrefix(s));
    }
    return result;
}
std::string GetSingleDevice() {
    auto v = GetSelectedDevices();
    return v.empty() ? "" : v[0];
}

bool IsDeviceFastboot(const std::string& serial) {
    int cnt = (int)SendMessage(g_hDeviceList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < cnt; i++) {
        int len = SendMessage(g_hDeviceList, LB_GETTEXTLEN, i, 0);
        std::wstring w(len + 1, 0);
        SendMessageW(g_hDeviceList, LB_GETTEXT, i, (LPARAM)&w[0]);
        w.resize(len);
        std::string s = WToU8(w);
        if (IsFastbootItem(s) && StripPrefix(s) == serial) return true;
    }
    return false;
}

void UpdateStatusBar() {
    auto devs = GetSelectedDevices();
    wchar_t buf[256];
    if (devs.empty())
        swprintf(buf, 256, L"  ● 未选中设备  |  ADB 工具 v3.2");
    else if (devs.size() == 1)
        swprintf(buf, 256, L"  ● 已连接: %s%s",
                 IsDeviceFastboot(devs[0]) ? L"[fastboot] " : L"",
                 U8ToW(devs[0]).c_str());
    else
        swprintf(buf, 256, L"  ● 已选中 %zu 台设备", devs.size());
    SetStatus(buf);
}

void RunForEachSelected(const std::string& subCmd) {
    auto devs = GetSelectedDevices();
    if (devs.empty()) { PostOutput("[错误] 未选中设备\r\n"); return; }
    for (auto& d : devs) {
        bool fb = IsDeviceFastboot(d);
        std::string full;
        if (fb) {
            if (subCmd.find("shell ") == 0 ||
                subCmd.find("install ") == 0 ||
                subCmd.find("push ") == 0 ||
                subCmd.find("pull ") == 0 ||
                subCmd.find("logcat") != std::string::npos ||
                subCmd.find("screencap") != std::string::npos) {
                PostOutput("[错误] 该命令不支持 fastboot 模式: " + subCmd + "\r\n");
                continue;
            }
            full = "fastboot -s " + d + " " + subCmd;
        } else {
            full = "adb -s " + d + " " + subCmd;
        }
        PostOutput("\r\n");
        PostOutput("[CMD] " + full + "\r\n");
        RunStreaming(full, [](const std::string& s){ PostOutput(s); });
    }
}

// ★ 同时查 adb 和 fastboot
void RefreshDeviceList() {
    std::vector<std::string> oldSel = GetSelectedDevices();
    SendMessage(g_hDeviceList, LB_RESETCONTENT, 0, 0);
    PostOutput("\r\n[刷新设备 adb + fastboot]\r\n");

    std::thread([oldSel]() {
        std::vector<std::string> adbDevs, fbDevs;

        // --- adb devices ---
        {
            std::string out;
            SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
            HANDLE r,w; CreatePipe(&r,&w,&sa,0);
            SetHandleInformation(r,HANDLE_FLAG_INHERIT,0);
            STARTUPINFOA si{sizeof(si)};
            si.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;
            si.wShowWindow=SW_HIDE; si.hStdOutput=w; si.hStdError=w;
            PROCESS_INFORMATION pi{};
            std::string cl = "cmd.exe /c adb devices";
            std::string clAnsi = U8ToAnsi(cl);
            std::vector<char> cb(clAnsi.begin(), clAnsi.end()); cb.push_back(0);
            if (CreateProcessA(NULL, cb.data(), NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) {
                CloseHandle(w);
                char buf[2048]; DWORD n=0; DWORD t0=GetTickCount();
                while (ReadFile(r,buf,sizeof(buf)-1,&n,NULL)&&n>0){
                    buf[n]=0; out+=buf;
                    if (GetTickCount()-t0 > 3000) break;
                }
                WaitForSingleObject(pi.hProcess, 2000);
                TerminateProcess(pi.hProcess, 0);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            } else CloseHandle(w);
            CloseHandle(r);

            PostOutput(out);
            std::istringstream iss(out);
            std::string line;
            while (std::getline(iss, line)) {
                if (line.find("\tdevice") != std::string::npos &&
                    line.find("List") == std::string::npos) {
                    size_t p = line.find('\t');
                    if (p != std::string::npos) adbDevs.push_back(line.substr(0, p));
                }
            }
        }

        // --- fastboot devices ---
        {
            std::string out;
            SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
            HANDLE r,w; CreatePipe(&r,&w,&sa,0);
            SetHandleInformation(r,HANDLE_FLAG_INHERIT,0);
            STARTUPINFOA si{sizeof(si)};
            si.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;
            si.wShowWindow=SW_HIDE; si.hStdOutput=w; si.hStdError=w;
            PROCESS_INFORMATION pi{};
            std::string cl = "cmd.exe /c fastboot devices";
            std::string clAnsi = U8ToAnsi(cl);
            std::vector<char> cb(clAnsi.begin(), clAnsi.end()); cb.push_back(0);
            if (CreateProcessA(NULL, cb.data(), NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) {
                CloseHandle(w);
                char buf[2048]; DWORD n=0; DWORD t0=GetTickCount();
                while (ReadFile(r,buf,sizeof(buf)-1,&n,NULL)&&n>0){
                    buf[n]=0; out+=buf;
                    if (GetTickCount()-t0 > 2000) break;
                }
                WaitForSingleObject(pi.hProcess, 1500);
                TerminateProcess(pi.hProcess, 0);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            } else CloseHandle(w);
            CloseHandle(r);

            if (!out.empty()) {
                PostOutput(out);
                std::istringstream iss(out);
                std::string line;
                while (std::getline(iss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty() || line.find("List") != std::string::npos) continue;
                    size_t p = line.find('\t');
                    std::string sn;
                    if (p != std::string::npos) sn = line.substr(0, p);
                    else { std::istringstream ls(line); ls >> sn; }
                    if (!sn.empty()) fbDevs.push_back(sn);
                }
            }
        }

        std::vector<std::string> allDevs;
        for (auto& d : adbDevs) allDevs.push_back("[adb] " + d);
        for (auto& d : fbDevs)  allDevs.push_back("[fastboot] " + d);

        struct Payload { std::vector<std::string> devs; std::vector<std::string> sel; };
        Payload* pl = new Payload{ allDevs, oldSel };
        PostMessage(g_hMainWnd, WM_DEVICES_READY, (WPARAM)pl, 0);
    }).detach();
}

// =========================================================
// adb shell
// =========================================================
void StopShell() {
    if (!g_shell.running.exchange(false)) return;
    if (g_shell.hStdIn) { CloseHandle(g_shell.hStdIn); g_shell.hStdIn = NULL; }
    if (g_shell.hProcess) {
        TerminateProcess(g_shell.hProcess, 0);
        CloseHandle(g_shell.hProcess);
        g_shell.hProcess = NULL;
    }
    if (g_shell.reader.joinable()) g_shell.reader.join();
    if (g_shell.hStdOut) { CloseHandle(g_shell.hStdOut); g_shell.hStdOut = NULL; }
    PostOutput("\r\n[Shell 已结束]\r\n");
}

bool StartShell(const std::string& device) {
    StopShell();
    SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
    HANDLE inR=NULL,inW=NULL,outR=NULL,outW=NULL;
    if (!CreatePipe(&inR, &inW, &sa, 0)) return false;
    if (!CreatePipe(&outR, &outW, &sa, 0)) {
        CloseHandle(inR); CloseHandle(inW); return false;
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError  = outW;

    PROCESS_INFORMATION pi{};
    std::string cmd = "cmd.exe /c adb";
    if (!device.empty()) cmd += " -s " + device;
    cmd += " shell";
    std::string cmdAnsi = U8ToAnsi(cmd);
    std::vector<char> cb(cmdAnsi.begin(), cmdAnsi.end()); cb.push_back(0);

    if (!CreateProcessA(NULL, cb.data(), NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(inR); CloseHandle(inW);
        CloseHandle(outR); CloseHandle(outW);
        return false;
    }
    CloseHandle(inR);
    CloseHandle(outW);
    CloseHandle(pi.hThread);

    g_shell.hProcess = pi.hProcess;
    g_shell.hStdIn = inW;
    g_shell.hStdOut = outR;
    g_shell.running = true;

    g_shell.reader = std::thread([]() {
        char buf[2048]; DWORD n = 0;
        while (g_shell.running.load()) {
            BOOL ok = ReadFile(g_shell.hStdOut, buf, sizeof(buf)-1, &n, NULL);
            if (!ok || n == 0) break;
            buf[n] = 0;
            PostOutput(std::string(buf, n));
        }
        g_shell.running = false;
    });

    PostOutput("\r\n[已进入 adb shell] 输入命令回车执行, exit 退出\r\n");
    return true;
}

void WriteShell(const std::string& lineUtf8) {
    if (!g_shell.running.load() || !g_shell.hStdIn) return;
    std::lock_guard<std::mutex> lk(g_shell.writeMtx);
    std::string sAnsi = U8ToAnsi(lineUtf8 + "\r\n");
    DWORD written = 0;
    WriteFile(g_shell.hStdIn, sAnsi.data(), (DWORD)sAnsi.size(), &written, NULL);
}

// =========================================================
// CMD 终端
// =========================================================
void StopTerm() {
    if (!g_term.running.exchange(false)) return;
    if (g_term.hStdIn) { CloseHandle(g_term.hStdIn); g_term.hStdIn = NULL; }
    if (g_term.hProcess) {
        TerminateProcess(g_term.hProcess, 0);
        CloseHandle(g_term.hProcess);
        g_term.hProcess = NULL;
    }
    if (g_term.reader.joinable()) g_term.reader.join();
    if (g_term.hStdOut) { CloseHandle(g_term.hStdOut); g_term.hStdOut = NULL; }
    PostTerm("\r\n[终端已关闭]\r\n");
}

bool StartTerm() {
    StopTerm();
    SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
    HANDLE inR=NULL,inW=NULL,outR=NULL,outW=NULL;
    if (!CreatePipe(&inR, &inW, &sa, 0)) return false;
    if (!CreatePipe(&outR, &outW, &sa, 0)) {
        CloseHandle(inR); CloseHandle(inW); return false;
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError  = outW;

    PROCESS_INFORMATION pi{};
    std::string cmd = "cmd.exe /Q /K chcp 65001 >nul & prompt $G";
    std::string cmdAnsi = U8ToAnsi(cmd);
    std::vector<char> cb(cmdAnsi.begin(), cmdAnsi.end()); cb.push_back(0);

    if (!CreateProcessA(NULL, cb.data(), NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(inR); CloseHandle(inW);
        CloseHandle(outR); CloseHandle(outW);
        return false;
    }
    CloseHandle(inR);
    CloseHandle(outW);
    CloseHandle(pi.hThread);

    g_term.hProcess = pi.hProcess;
    g_term.hStdIn = inW;
    g_term.hStdOut = outR;
    g_term.running = true;

    g_term.reader = std::thread([]() {
        char buf[2048]; DWORD n = 0;
        while (g_term.running.load()) {
            BOOL ok = ReadFile(g_term.hStdOut, buf, sizeof(buf)-1, &n, NULL);
            if (!ok || n == 0) break;
            buf[n] = 0;
            PostTerm(std::string(buf, n));
        }
        g_term.running = false;
        PostMessage(g_hMainWnd, WM_TERM_DEAD, 0, 0);
    });

    PostTerm("Microsoft Windows [命令行终端]\r\n");
    PostTerm("(C) 保留所有权利。\r\n\r\n");
    return true;
}

void WriteTerm(const std::string& lineUtf8) {
    if (!g_term.running.load() || !g_term.hStdIn) return;
    std::lock_guard<std::mutex> lk(g_term.writeMtx);
    std::string sAnsi = U8ToAnsi(lineUtf8 + "\r\n");
    DWORD written = 0;
    WriteFile(g_term.hStdIn, sAnsi.data(), (DWORD)sAnsi.size(), &written, NULL);
}

// =========================================================
// logcat
// =========================================================
void StopLogcat() {
    if (!g_logcatRunning.exchange(false)) return;
    if (g_logcatProc) {
        TerminateProcess(g_logcatProc, 0);
        CloseHandle(g_logcatProc); g_logcatProc = NULL;
    }
    if (g_logcatReader.joinable()) g_logcatReader.join();
    if (g_logcatOut) { CloseHandle(g_logcatOut); g_logcatOut = NULL; }
    PostOutput("\r\n[logcat 已停止]\r\n");
    if (g_hLogcatBtn) SetWindowTextW(g_hLogcatBtn, L"实时 Logcat");
}
bool StartLogcat(const std::string& device) {
    StopLogcat();
    SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
    HANDLE r=NULL, w=NULL;
    if (!CreatePipe(&r, &w, &sa, 0)) return false;
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = w; si.hStdError = w;

    PROCESS_INFORMATION pi{};
    std::string cmd = "cmd.exe /c adb";
    if (!device.empty()) cmd += " -s " + device;
    cmd += " logcat -v time";
    std::string cmdAnsi = U8ToAnsi(cmd);
    std::vector<char> cb(cmdAnsi.begin(), cmdAnsi.end()); cb.push_back(0);

    if (!CreateProcessA(NULL, cb.data(), NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(r); CloseHandle(w); return false;
    }
    CloseHandle(w);
    CloseHandle(pi.hThread);

    g_logcatProc = pi.hProcess;
    g_logcatOut = r;
    g_logcatRunning = true;

    g_logcatReader = std::thread([]() {
        char buf[2048]; DWORD n = 0;
        while (g_logcatRunning.load()) {
            BOOL ok = ReadFile(g_logcatOut, buf, sizeof(buf)-1, &n, NULL);
            if (!ok || n == 0) break;
            buf[n] = 0;
            PostOutput(std::string(buf, n));
        }
        g_logcatRunning = false;
    });

    PostOutput("\r\n[logcat 已开启]\r\n");
    if (g_hLogcatBtn) SetWindowTextW(g_hLogcatBtn, L"停止 Logcat");
    return true;
}

// =========================================================
// scrcpy 查找
// =========================================================
bool FileExists(const std::string& pathUtf8) {
    std::wstring w = U8ToW(pathUtf8);
    DWORD attr = GetFileAttributesW(w.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::string GetExeDir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p = p.substr(0, pos);
    return WToU8(p);
}

std::string ReadScrcpyFromIni() {
    g_iniPath = GetExeDir() + "\\adb_gui.ini";
    wchar_t buf[MAX_PATH] = {0};
    std::wstring iniW = U8ToW(g_iniPath);
    GetPrivateProfileStringW(L"scrcpy", L"path", L"",
                             buf, MAX_PATH, iniW.c_str());
    if (wcslen(buf) == 0) return "";
    return WToU8(buf);
}
void WriteScrcpyToIni(const std::string& pathUtf8) {
    if (g_iniPath.empty()) g_iniPath = GetExeDir() + "\\adb_gui.ini";
    std::wstring iniW = U8ToW(g_iniPath);
    std::wstring pathW = U8ToW(pathUtf8);
    WritePrivateProfileStringW(L"scrcpy", L"path", pathW.c_str(), iniW.c_str());
}

std::string FindInPath() {
    wchar_t buf[MAX_PATH] = {0};
    if (SearchPathW(NULL, L"scrcpy.exe", NULL, MAX_PATH, buf, NULL))
        return WToU8(buf);
    return "";
}

// ★ 枚举所有盘符
std::string ScanAllDrives() {
    const char* subPaths[] = {
        "\\scrcpy\\scrcpy.exe",
        "\\scrcpy-win64\\scrcpy.exe",
        "\\scrcpy-win32\\scrcpy.exe",
        "\\Tools\\scrcpy\\scrcpy.exe",
        "\\tools\\scrcpy\\scrcpy.exe",
        "\\Program Files\\scrcpy\\scrcpy.exe",
        "\\Program Files (x86)\\scrcpy\\scrcpy.exe",
        "\\green\\scrcpy\\scrcpy.exe",
        "\\soft\\scrcpy\\scrcpy.exe",
        "\\软件\\scrcpy\\scrcpy.exe",
    };

    DWORD drives = GetLogicalDrives();
    char driveRoot[4] = "A:\\";

    for (int i = 0; i < 26; i++) {
        if (!(drives & (1 << i))) continue;
        driveRoot[0] = (char)('A' + i);
        UINT type = GetDriveTypeA(driveRoot);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;

        for (auto sub : subPaths) {
            std::string p = std::string(driveRoot) + sub;
            if (FileExists(p)) return p;
        }
    }
    return "";
}

std::string LocateScrcpy() {
    std::string fromIni = ReadScrcpyFromIni();
    if (!fromIni.empty() && FileExists(fromIni)) return fromIni;

    std::string sameDir = GetExeDir() + "\\scrcpy.exe";
    if (FileExists(sameDir)) return sameDir;

    std::string fromDrives = ScanAllDrives();
    if (!fromDrives.empty()) return fromDrives;

    wchar_t userProfile[MAX_PATH] = {0};
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH)) {
        std::string up = WToU8(userProfile);
        const char* rel[] = {
            "\\scrcpy\\scrcpy.exe",
            "\\Downloads\\scrcpy\\scrcpy.exe",
            "\\Desktop\\scrcpy\\scrcpy.exe",
        };
        for (auto r : rel) {
            std::string p = up + r;
            if (FileExists(p)) return p;
        }
    }

    std::string fromPath = FindInPath();
    if (!fromPath.empty()) return fromPath;

    return "";
}

std::string PromptForScrcpy(HWND hOwner) {
    OPENFILENAMEW ofn{sizeof(ofn)};
    wchar_t f[MAX_PATH] = {0};
    ofn.hwndOwner = hOwner;
    ofn.lpstrTitle = L"请选择 scrcpy.exe";
    ofn.lpstrFilter = L"scrcpy.exe\0scrcpy.exe\0可执行文件\0*.exe\0";
    ofn.lpstrFile = f;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return "";

    std::string path = WToU8(f);
    std::wstring w = U8ToW(path);
    size_t pos = w.find_last_of(L"\\/");
    std::wstring name = (pos != std::wstring::npos) ? w.substr(pos + 1) : w;
    if (_wcsicmp(name.c_str(), L"scrcpy.exe") != 0) {
        MessageBoxW(hOwner,
            L"请选择 scrcpy.exe 本身, 不是其他文件。",
            L"错误", MB_ICONWARNING);
        return "";
    }
    WriteScrcpyToIni(path);
    return path;
}

void StartScrcpy(const std::string& device) {
    if (g_scrcpyRunning.load()) {
        int r = MessageBoxW(g_hMainWnd,
            L"scrcpy 已在运行, 是否关闭后重新启动?",
            L"提示", MB_YESNO | MB_ICONQUESTION);
        if (r != IDYES) return;
        if (g_scrcpyPi.hProcess) TerminateProcess(g_scrcpyPi.hProcess, 0);
        g_scrcpyRunning = false;
        Sleep(300);
    }

    if (g_scrcpyPath.empty() || !FileExists(g_scrcpyPath)) {
        g_scrcpyPath = LocateScrcpy();
    }

    if (g_scrcpyPath.empty()) {
        int r = MessageBoxW(g_hMainWnd,
            L"未找到 scrcpy.exe。\n\n"
            L"是否现在手动指定路径?\n"
            L"(选定后会记住, 下次自动使用)",
            L"找不到 scrcpy", MB_YESNO | MB_ICONQUESTION);
        if (r != IDYES) return;

        std::string picked = PromptForScrcpy(g_hMainWnd);
        if (picked.empty()) return;
        g_scrcpyPath = picked;
    }

    std::string cmd = "\"" + g_scrcpyPath + "\"";
    if (!device.empty()) cmd += " -s " + device;
    cmd += " --window-title \"scrcpy - " + device + "\"";

    PostOutput("\r\n[scrcpy] " + cmd + "\r\n");

    std::wstring scrcpyDir = U8ToW(g_scrcpyPath);
    size_t p = scrcpyDir.find_last_of(L"\\/");
    if (p != std::wstring::npos) scrcpyDir = scrcpyDir.substr(0, p);

    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;

    std::wstring cmdW = U8ToW(cmd);
    std::vector<wchar_t> cmdBuf(cmdW.begin(), cmdW.end());
    cmdBuf.push_back(0);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(
        U8ToW(g_scrcpyPath).c_str(),
        cmdBuf.data(),
        NULL, NULL, FALSE,
        0, NULL,
        scrcpyDir.c_str(),
        &si, &pi);

    if (!ok) {
        DWORD err = GetLastError();
        char msg[256];
        sprintf(msg, "[错误] 启动 scrcpy 失败 (错误码 %lu)\r\n", err);
        PostOutput(msg);
        MessageBoxW(g_hMainWnd, L"启动 scrcpy 失败。", L"错误", MB_ICONERROR);
        return;
    }

    PostOutput("[scrcpy 已启动, 请查看独立窗口]\r\n");
    CloseHandle(pi.hThread);
    g_scrcpyPi = pi;
    g_scrcpyRunning = true;

    std::thread([](){
        WaitForSingleObject(g_scrcpyPi.hProcess, INFINITE);
        g_scrcpyRunning = false;
        if (g_scrcpyPi.hProcess) CloseHandle(g_scrcpyPi.hProcess);
        g_scrcpyPi.hProcess = NULL;
        PostOutput("\r\n[scrcpy 已退出]\r\n");
    }).detach();
}

// =========================================================
// 自绘按钮
// =========================================================
void DrawButton(LPDRAWITEMSTRUCT dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    HWND hBtn = dis->hwndItem;
    int id = GetDlgCtrlID(hBtn);

    bool isAccent = false;
    if (id == IDC_REFRESH_BTN || id == IDC_EXECUTE_BTN ||
        id == IDC_TERM_SEND || id == IDC_INSTALL_BTN ||
        id == IDC_SCRCPY_BTN) isAccent = true;

    int state = 0;
    if (dis->itemState & ODS_SELECTED) state = 2;
    else {
        auto it = g_btnHover.find(hBtn);
        if (it != g_btnHover.end() && it->second) state = 1;
    }

    COLORREF bg, fg, border;
    if (isAccent) {
        if (state == 2) bg = COL_ACCENT_PRS;
        else if (state == 1) bg = COL_ACCENT_HOV;
        else bg = COL_ACCENT;
        fg = RGB(255,255,255);
        border = bg;
    } else {
        if (state == 2) bg = RGB(60,60,65);
        else if (state == 1) bg = RGB(55,55,60);
        else bg = COL_PANEL2;
        fg = COL_TEXT;
        border = COL_BORDER;
    }

    HBRUSH br = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    wchar_t text[128] = {0};
    GetWindowTextW(hBtn, text, 127);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_hFontUI);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

// =========================================================
// 文件管理器
// =========================================================
#define IDC_FILE_PATH   2101
#define IDC_FILE_LIST   2102
#define IDC_FILE_UP     2103
#define IDC_FILE_OPEN   2104
#define IDC_FILE_PULL   2105
#define IDC_FILE_PUSH   2106
#define IDC_FILE_DEL    2107

HWND g_hFileWnd = NULL;
HWND g_hFilePath = NULL;
HWND g_hFileList = NULL;
std::string g_curPath = "/sdcard";

void RefreshFileList() {
    if (!g_hFileList) return;
    SendMessage(g_hFileList, LB_RESETCONTENT, 0, 0);
    SetWindowTextW(g_hFilePath, U8ToW(g_curPath).c_str());
    std::string dev = GetSingleDevice();
    std::string cmd = "adb";
    if (!dev.empty()) cmd += " -s " + dev;
    cmd += " shell ls -a -p \"" + g_curPath + "\"";
    RunStreaming(cmd, [](const std::string& s) {
        std::istringstream iss(s);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::wstring w = U8ToW(line);
            SendMessageW(g_hFileList, LB_ADDSTRING, 0, (LPARAM)w.c_str());
        }
    });
}

LRESULT CALLBACK FileWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_PANEL);
        return (LRESULT)g_brPanel;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, g_brBg);
        return 1;
    }
    case WM_CREATE: {
        HWND hLbl = CreateWindowW(L"STATIC", L"路径:",
            WS_CHILD|WS_VISIBLE, 12,14,40,20,hWnd,NULL,NULL,NULL);
        SendMessage(hLbl, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        g_hFilePath = CreateWindowW(L"EDIT", L"/sdcard",
            WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            58,12,380,24,hWnd,(HMENU)(INT_PTR)IDC_FILE_PATH,NULL,NULL);
        SendMessage(g_hFilePath, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);

        HWND b1 = CreateWindowW(L"BUTTON", L"转到", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            445,12,60,24,hWnd,(HMENU)(INT_PTR)IDC_FILE_OPEN,NULL,NULL);
        HWND b2 = CreateWindowW(L"BUTTON", L"上级", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            510,12,60,24,hWnd,(HMENU)(INT_PTR)IDC_FILE_UP,NULL,NULL);
        HWND b3 = CreateWindowW(L"BUTTON", L"推送", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            445,44,60,24,hWnd,(HMENU)(INT_PTR)IDC_FILE_PUSH,NULL,NULL);
        HWND b4 = CreateWindowW(L"BUTTON", L"拉取", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            510,44,60,24,hWnd,(HMENU)(INT_PTR)IDC_FILE_PULL,NULL,NULL);
        HWND b6 = CreateWindowW(L"BUTTON", L"删除", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            380,44,60,24,hWnd,(HMENU)(INT_PTR)IDC_FILE_DEL,NULL,NULL);

        HWND btns[] = {b1,b2,b3,b4,b6};
        for (HWND b : btns) {
            SendMessage(b, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            g_btnStyles[b] = {false, true};
            g_btnHover[b] = 0;
        }

        g_hFileList = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|
            LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,
            12,78,558,380,hWnd,(HMENU)IDC_FILE_LIST,NULL,NULL);
        SendMessage(g_hFileList, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        RefreshFileList();
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType == ODT_BUTTON) { DrawButton(dis); return TRUE; }
        if (dis->CtlType == ODT_LISTBOX) {
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            bool sel = (dis->itemState & ODS_SELECTED) != 0;
            COLORREF bg = sel ? COL_ACCENT : COL_PANEL;
            COLORREF fg = sel ? RGB(255,255,255) : COL_TEXT;
            if (dis->itemID == (UINT)-1) return TRUE;
            HBRUSH br = CreateSolidBrush(bg);
            FillRect(hdc, &rc, br);
            DeleteObject(br);
            wchar_t text[512] = {0};
            SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, fg);
            HFONT old = (HFONT)SelectObject(hdc, g_hFontUI);
            RECT rt = rc; rt.left += 6;
            DrawTextW(hdc, text, -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_FILE_OPEN: {
            wchar_t buf[512] = {0};
            GetWindowTextW(g_hFilePath, buf, 511);
            g_curPath = WToU8(buf);
            RefreshFileList();
            break;
        }
        case IDC_FILE_UP: {
            if (g_curPath.size() > 1) {
                if (g_curPath.back() == '/') g_curPath.pop_back();
                size_t p = g_curPath.rfind('/');
                if (p == 0) g_curPath = "/";
                else if (p != std::string::npos) g_curPath = g_curPath.substr(0, p);
                RefreshFileList();
            }
            break;
        }
        case IDC_FILE_DEL: {
            int i = SendMessage(g_hFileList, LB_GETCURSEL, 0, 0);
            if (i == LB_ERR) break;
            int len = SendMessage(g_hFileList, LB_GETTEXTLEN, i, 0);
            std::wstring w(len + 1, 0);
            SendMessageW(g_hFileList, LB_GETTEXT, i, (LPARAM)&w[0]);
            w.resize(len);
            std::string name = WToU8(w);
            if (name == "." || name == "..") break;
            if (MessageBoxW(hWnd, (L"删除 " + U8ToW(name) + L" ?").c_str(),
                            L"确认", MB_YESNO) == IDYES) {
                std::string target = g_curPath;
                if (target.back() != '/') target += "/";
                target += name;
                if (!name.empty() && name.back() == '/') target.pop_back();
                RunToOutput("adb shell rm -rf \"" + target + "\"");
                std::thread([]{ Sleep(800); RefreshFileList(); }).detach();
            }
            break;
        }
        case IDC_FILE_LIST:
            if (HIWORD(wParam) == LBN_DBLCLK) {
                int i = SendMessage(g_hFileList, LB_GETCURSEL, 0, 0);
                if (i == LB_ERR) break;
                int len = SendMessage(g_hFileList, LB_GETTEXTLEN, i, 0);
                std::wstring w(len + 1, 0);
                SendMessageW(g_hFileList, LB_GETTEXT, i, (LPARAM)&w[0]);
                w.resize(len);
                std::string name = WToU8(w);
                if (name == "." || name == "..") break;
                if (!name.empty() && name.back() == '/') {
                    if (g_curPath.back() != '/') g_curPath += "/";
                    g_curPath += name.substr(0, name.size()-1);
                    RefreshFileList();
                }
            }
            break;
        case IDC_FILE_PUSH: {
            OPENFILENAMEW ofn{sizeof(ofn)};
            wchar_t f[MAX_PATH] = {0};
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = L"所有文件\0*.*\0";
            ofn.lpstrFile = f;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                std::string local = WToU8(f);
                RunToOutput("adb push \"" + local + "\" \"" + g_curPath + "\"");
                std::thread([]{ Sleep(1500); RefreshFileList(); }).detach();
            }
            break;
        }
        case IDC_FILE_PULL: {
            int i = SendMessage(g_hFileList, LB_GETCURSEL, 0, 0);
            if (i == LB_ERR) break;
            int len = SendMessage(g_hFileList, LB_GETTEXTLEN, i, 0);
            std::wstring w(len + 1, 0);
            SendMessageW(g_hFileList, LB_GETTEXT, i, (LPARAM)&w[0]);
            w.resize(len);
            std::string name = WToU8(w);
            if (name.empty() || name.back() == '/') break;
            wchar_t desk[MAX_PATH];
            SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desk);
            std::string remote = g_curPath;
            if (remote.back() != '/') remote += "/";
            remote += name;
            std::string local = WToU8(desk) + "\\" + name;
            RunToOutput("adb pull \"" + remote + "\" \"" + local + "\"");
            break;
        }
        }
        break;
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND hCur = ChildWindowFromPoint(hWnd, pt);
        for (auto& kv : g_btnHover) {
            if (!IsWindow(kv.first)) continue;
            bool now = (kv.first == hCur);
            if ((int)now != kv.second) { kv.second = now; InvalidateRect(kv.first, NULL, TRUE); }
        }
        break;
    }
    case WM_CLOSE: DestroyWindow(hWnd); break;
    case WM_DESTROY: g_hFileWnd = NULL; g_hFileList = g_hFilePath = NULL; break;
    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowFileManager() {
    if (g_hFileWnd) { SetForegroundWindow(g_hFileWnd); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = FileWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"AdbFileWndV32";
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        RegisterClassExW(&wc);
        reg = true;
    }
    g_hFileWnd = CreateWindowExW(0, L"AdbFileWndV32", L"文件管理器",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 600, 520,
        g_hMainWnd, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(g_hFileWnd, SW_SHOW);
}

// =========================================================
// 进程管理
// =========================================================
#define IDC_PROC_LIST   3101
#define IDC_PROC_KILL   3102
#define IDC_PROC_RELOAD 3103
#define IDC_PROC_FILTER 3104

HWND g_hProcWnd = NULL;
HWND g_hProcList = NULL;
HWND g_hProcFilter = NULL;

void RefreshProcessList() {
    if (!g_hProcList) return;
    SendMessage(g_hProcList, LB_RESETCONTENT, 0, 0);
    wchar_t buf[128] = {0};
    if (g_hProcFilter) GetWindowTextW(g_hProcFilter, buf, 127);
    std::string filter = WToU8(buf);

    std::string dev = GetSingleDevice();
    std::string cmd = "adb";
    if (!dev.empty()) cmd += " -s " + dev;
    cmd += " shell ps -A";

    RunStreaming(cmd, [filter](const std::string& s) {
        std::istringstream iss(s);
        std::string line;
        bool first = true;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (first) { first = false; continue; }
            if (!filter.empty() && line.find(filter) == std::string::npos) continue;
            std::wstring w = U8ToW(line);
            SendMessageW(g_hProcList, LB_ADDSTRING, 0, (LPARAM)w.c_str());
        }
    });
}

LRESULT CALLBACK ProcWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_PANEL);
        return (LRESULT)g_brPanel;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, g_brBg);
        return 1;
    }
    case WM_CREATE: {
        HWND hReload = CreateWindowW(L"BUTTON", L"刷新", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            10,10,80,26,hWnd,(HMENU)(INT_PTR)IDC_PROC_RELOAD,NULL,NULL);
        HWND hKill = CreateWindowW(L"BUTTON", L"结束进程", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            100,10,100,26,hWnd,(HMENU)(INT_PTR)IDC_PROC_KILL,NULL,NULL);
        HWND btns[] = {hReload, hKill};
        for (HWND b : btns) {
            SendMessage(b, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            g_btnStyles[b] = {false, false};
            g_btnHover[b] = 0;
        }
        g_hProcFilter = CreateWindowW(L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            210,10,200,26,hWnd,(HMENU)IDC_PROC_FILTER,NULL,NULL);
        SendMessage(g_hProcFilter, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        g_hProcList = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,
            10,45,700,450,hWnd,(HMENU)IDC_PROC_LIST,NULL,NULL);
        SendMessage(g_hProcList, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
        RefreshProcessList();
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType == ODT_BUTTON) { DrawButton(dis); return TRUE; }
        break;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND hCur = ChildWindowFromPoint(hWnd, pt);
        for (auto& kv : g_btnHover) {
            if (!IsWindow(kv.first)) continue;
            bool now = (kv.first == hCur);
            if ((int)now != kv.second) { kv.second = now; InvalidateRect(kv.first, NULL, TRUE); }
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PROC_RELOAD) RefreshProcessList();
        else if (LOWORD(wParam) == IDC_PROC_FILTER) {
            if (HIWORD(wParam) == EN_CHANGE) RefreshProcessList();
        } else if (LOWORD(wParam) == IDC_PROC_KILL) {
            int i = SendMessage(g_hProcList, LB_GETCURSEL, 0, 0);
            if (i == LB_ERR) break;
            int len = SendMessage(g_hProcList, LB_GETTEXTLEN, i, 0);
            std::wstring w(len + 1, 0);
            SendMessageW(g_hProcList, LB_GETTEXT, i, (LPARAM)&w[0]);
            w.resize(len);
            std::string line = WToU8(w);
            std::istringstream iss(line);
            std::string user, pid;
            iss >> user >> pid;
            if (!pid.empty()) {
                if (MessageBoxW(hWnd, (L"结束进程 PID " + U8ToW(pid) + L" ?").c_str(),
                                L"确认", MB_YESNO) == IDYES)
                    RunForEachSelected("shell kill -9 " + pid);
            }
        }
        break;
    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        if (g_hProcList) SetWindowPos(g_hProcList, NULL, 10, 45, rc.right-20, rc.bottom-55, SWP_NOZORDER);
        break;
    }
    case WM_CLOSE: DestroyWindow(hWnd); break;
    case WM_DESTROY: g_hProcWnd = NULL; g_hProcList = NULL; g_hProcFilter = NULL; break;
    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowProcessManager() {
    if (g_hProcWnd) { SetForegroundWindow(g_hProcWnd); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = ProcWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"AdbProcWndV32";
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        RegisterClassExW(&wc);
        reg = true;
    }
    g_hProcWnd = CreateWindowExW(0, L"AdbProcWndV32", L"进程管理",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 740, 560,
        g_hMainWnd, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(g_hProcWnd, SW_SHOW);
}

// =========================================================
// 应用管理
// =========================================================
#define IDC_APP_LIST    4101
#define IDC_APP_RELOAD  4102
#define IDC_APP_UNINST  4103
#define IDC_APP_START   4104
#define IDC_APP_STOP    4105
#define IDC_APP_FILTER  4106
#define IDC_APP_INFO    4107

HWND g_hAppWnd = NULL;
HWND g_hAppList = NULL;
HWND g_hAppFilter = NULL;

void RefreshAppList() {
    if (!g_hAppList) return;
    SendMessage(g_hAppList, LB_RESETCONTENT, 0, 0);
    wchar_t buf[128] = {0};
    if (g_hAppFilter) GetWindowTextW(g_hAppFilter, buf, 127);
    std::string filter = WToU8(buf);

    std::string dev = GetSingleDevice();
    std::string cmd = "adb";
    if (!dev.empty()) cmd += " -s " + dev;
    cmd += " shell pm list packages";

    RunStreaming(cmd, [filter](const std::string& s) {
        std::istringstream iss(s);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            size_t p = line.find("package:");
            if (p == std::string::npos) continue;
            std::string pkg = line.substr(p + 8);
            if (!filter.empty() && pkg.find(filter) == std::string::npos) continue;
            std::wstring w = U8ToW(pkg);
            SendMessageW(g_hAppList, LB_ADDSTRING, 0, (LPARAM)w.c_str());
        }
    });
}

LRESULT CALLBACK AppWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_PANEL);
        return (LRESULT)g_brPanel;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, g_brBg);
        return 1;
    }
    case WM_CREATE: {
        HWND hReload = CreateWindowW(L"BUTTON", L"刷新", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            10,10,70,26,hWnd,(HMENU)(INT_PTR)IDC_APP_RELOAD,NULL,NULL);
        g_hAppFilter = CreateWindowW(L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            90,10,200,26,hWnd,(HMENU)IDC_APP_FILTER,NULL,NULL);
        SendMessage(g_hAppFilter, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        HWND hUninst = CreateWindowW(L"BUTTON", L"卸载", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            300,10,70,26,hWnd,(HMENU)(INT_PTR)IDC_APP_UNINST,NULL,NULL);
        HWND hStart = CreateWindowW(L"BUTTON", L"启动", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            375,10,70,26,hWnd,(HMENU)(INT_PTR)IDC_APP_START,NULL,NULL);
        HWND hStop = CreateWindowW(L"BUTTON", L"强停", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            450,10,70,26,hWnd,(HMENU)(INT_PTR)IDC_APP_STOP,NULL,NULL);
        HWND hInfo = CreateWindowW(L"BUTTON", L"信息", WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            525,10,70,26,hWnd,(HMENU)(INT_PTR)IDC_APP_INFO,NULL,NULL);
        HWND btns[] = {hReload, hUninst, hStart, hStop, hInfo};
        for (HWND b : btns) {
            SendMessage(b, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            g_btnStyles[b] = {false, false};
            g_btnHover[b] = 0;
        }
        g_hAppList = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOINTEGRALHEIGHT|LBS_NOTIFY,
            10,45,620,450,hWnd,(HMENU)IDC_APP_LIST,NULL,NULL);
        SendMessage(g_hAppList, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        RefreshAppList();
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType == ODT_BUTTON) { DrawButton(dis); return TRUE; }
        break;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND hCur = ChildWindowFromPoint(hWnd, pt);
        for (auto& kv : g_btnHover) {
            if (!IsWindow(kv.first)) continue;
            bool now = (kv.first == hCur);
            if ((int)now != kv.second) { kv.second = now; InvalidateRect(kv.first, NULL, TRUE); }
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_APP_RELOAD: RefreshAppList(); break;
        case IDC_APP_FILTER: if (HIWORD(wParam) == EN_CHANGE) RefreshAppList(); break;
        case IDC_APP_UNINST:
        case IDC_APP_START:
        case IDC_APP_STOP:
        case IDC_APP_INFO: {
            int i = SendMessage(g_hAppList, LB_GETCURSEL, 0, 0);
            if (i == LB_ERR) break;
            int len = SendMessage(g_hAppList, LB_GETTEXTLEN, i, 0);
            std::wstring w(len + 1, 0);
            SendMessageW(g_hAppList, LB_GETTEXT, i, (LPARAM)&w[0]);
            w.resize(len);
            std::string pkg = WToU8(w);
            int id = LOWORD(wParam);
            if (id == IDC_APP_UNINST) {
                if (MessageBoxW(hWnd, (L"卸载 " + U8ToW(pkg) + L" ?").c_str(), L"确认", MB_YESNO) == IDYES)
                    RunForEachSelected("uninstall " + pkg);
            } else if (id == IDC_APP_START) {
                RunForEachSelected("shell monkey -p " + pkg + " -c android.intent.category.LAUNCHER 1");
            } else if (id == IDC_APP_STOP) {
                RunForEachSelected("shell am force-stop " + pkg);
            } else {
                RunForEachSelected("shell dumpsys package " + pkg);
            }
            break;
        }
        }
        break;
    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        if (g_hAppList) SetWindowPos(g_hAppList, NULL, 10, 45, rc.right-20, rc.bottom-55, SWP_NOZORDER);
        break;
    }
    case WM_CLOSE: DestroyWindow(hWnd); break;
    case WM_DESTROY: g_hAppWnd = NULL; g_hAppList = g_hAppFilter = NULL; break;
    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowAppManager() {
    if (g_hAppWnd) { SetForegroundWindow(g_hAppWnd); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = AppWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"AdbAppWndV32";
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        RegisterClassExW(&wc);
        reg = true;
    }
    g_hAppWnd = CreateWindowExW(0, L"AdbAppWndV32", L"应用管理",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 660, 560,
        g_hMainWnd, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(g_hAppWnd, SW_SHOW);
}

// =========================================================
// 安装 APK (含中文路径处理)
// =========================================================
void InstallApkSmart(HWND hOwner) {
    OPENFILENAMEW ofn{sizeof(ofn)};
    wchar_t f[MAX_PATH] = {0};
    ofn.hwndOwner = hOwner;
    ofn.lpstrFilter = L"APK 文件\0*.apk\0所有文件\0*.*\0";
    ofn.lpstrFile = f;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    std::string pathUtf8 = WToU8(f);
    if (IsAscii(pathUtf8)) {
        RunForEachSelected("install -r \"" + pathUtf8 + "\"");
        return;
    }
    char tempDirA[MAX_PATH] = {0};
    GetTempPathA(MAX_PATH, tempDirA);
    SYSTEMTIME st; GetLocalTime(&st);
    char tmpName[64];
    sprintf(tmpName, "adb_install_%04d%02d%02d_%02d%02d%02d.apk",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::string tmpPathA = std::string(tempDirA) + tmpName;
    std::wstring tmpPathW = U8ToW(tmpPathA);

    PostOutput("[提示] 检测到中文路径, 已复制到临时目录\r\n");
    if (!CopyFileW(f, tmpPathW.c_str(), FALSE)) {
        PostOutput("[错误] 复制失败, 请把 APK 重命名为英文再试\r\n");
        MessageBoxW(hOwner, L"复制 APK 到临时目录失败。\n请重命名为英文后重试。",
                    L"错误", MB_ICONERROR);
        return;
    }
    RunForEachSelected("install -r \"" + tmpPathA + "\"");
    std::thread([tmpPathW]() {
        Sleep(30000);
        DeleteFileW(tmpPathW.c_str());
    }).detach();
}

// =========================================================
// ADB 页
// =========================================================
void CreateAdbPage(HWND hParent) {
    HWND hLbl1 = CreateWindowW(L"STATIC", L"设备列表 (Ctrl/Shift 多选):",
        WS_CHILD|WS_VISIBLE, 14,10,280,20,hParent,NULL,NULL,NULL);
    SendMessage(hLbl1, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

    g_hDeviceList = CreateWindowW(L"LISTBOX", NULL,
        WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|
        LBS_EXTENDEDSEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,
        14,32,260,170,hParent,(HMENU)IDC_DEVICE_LIST,NULL,NULL);
    SendMessage(g_hDeviceList, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

    struct BtnDef { const wchar_t* t; int id; };
    BtnDef leftBtns[] = {
        { L"刷新设备",     IDC_REFRESH_BTN    },
        { L"安装 APK",     IDC_INSTALL_BTN    },
        { L"截图",         IDC_SCREENSHOT_BTN },
        { L"文件管理器",   IDC_FILES_BTN      },
        { L"进程管理",     IDC_PROCS_BTN      },
        { L"应用管理",     IDC_APPS_BTN       },
    };
    BtnDef rightBtns[] = {
        { L"无线 ADB...",   IDC_WIRELESS_BTN   },
        { L"重启设备",      IDC_REBOOT_BTN     },
        { L"重启 Recovery", IDC_REBOOT_RECOV   },
        { L"重启 Fastboot", IDC_REBOOT_BOOT    },
        { L"设备信息",      IDC_INFO_BTN       },
        { L"电池信息",      IDC_BATTERY_BTN    },
    };
    for (int i = 0; i < 6; i++) {
        HWND b1 = CreateWindowW(L"BUTTON", leftBtns[i].t,
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            286, 32 + i*30, 130, 26, hParent,
            (HMENU)(INT_PTR)leftBtns[i].id, NULL, NULL);
        HWND b2 = CreateWindowW(L"BUTTON", rightBtns[i].t,
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            424, 32 + i*30, 130, 26, hParent,
            (HMENU)(INT_PTR)rightBtns[i].id, NULL, NULL);
        SendMessage(b1, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        SendMessage(b2, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        g_btnStyles[b1] = {leftBtns[i].id == IDC_REFRESH_BTN, false};
        g_btnStyles[b2] = {false, false};
        g_btnHover[b1] = 0;
        g_btnHover[b2] = 0;
        g_adbPageCtrls.push_back(b1);
        g_adbPageCtrls.push_back(b2);
    }

    g_hShellBtn = CreateWindowW(L"BUTTON", L"进入 adb Shell",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW, 562, 32, 130, 38,
        hParent, (HMENU)IDC_SHELL_BTN, NULL, NULL);
    SendMessage(g_hShellBtn, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    g_btnStyles[g_hShellBtn] = {false, false};
    g_btnHover[g_hShellBtn] = 0;
    g_adbPageCtrls.push_back(g_hShellBtn);

    g_hLogcatBtn = CreateWindowW(L"BUTTON", L"实时 Logcat",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW, 562, 74, 130, 38,
        hParent, (HMENU)IDC_LOGCAT_BTN, NULL, NULL);
    SendMessage(g_hLogcatBtn, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    g_btnStyles[g_hLogcatBtn] = {false, false};
    g_btnHover[g_hLogcatBtn] = 0;
    g_adbPageCtrls.push_back(g_hLogcatBtn);

    HWND hScrcpy = CreateWindowW(L"BUTTON", L"scrcpy 投屏",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW, 562, 116, 130, 38,
        hParent, (HMENU)IDC_SCRCPY_BTN, NULL, NULL);
    SendMessage(hScrcpy, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    g_btnStyles[hScrcpy] = {true, false};
    g_btnHover[hScrcpy] = 0;
    g_adbPageCtrls.push_back(hScrcpy);

    HWND hScrcpySet = CreateWindowW(L"BUTTON", L"⚙",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW, 696, 116, 24, 38,
        hParent, (HMENU)IDC_SCRCPY_SET, NULL, NULL);
    SendMessage(hScrcpySet, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    g_btnStyles[hScrcpySet] = {false, false};
    g_btnHover[hScrcpySet] = 0;
    g_adbPageCtrls.push_back(hScrcpySet);

    HWND hLbl2 = CreateWindowW(L"STATIC", L"输出:",
        WS_CHILD|WS_VISIBLE, 14,215,100,20,hParent,NULL,NULL,NULL);
    SendMessage(hLbl2, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

    HWND hClear = CreateWindowW(L"BUTTON", L"清空",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
        590, 210, 100, 26, hParent, (HMENU)IDC_CLEAR_BTN, NULL, NULL);
    SendMessage(hClear, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    g_btnStyles[hClear] = {false, false};
    g_btnHover[hClear] = 0;

    g_hOutputEdit = CreateWindowW(L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|WS_HSCROLL|
        ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_READONLY,
        14, 238, 676, 240, hParent, (HMENU)IDC_OUTPUT_EDIT, NULL, NULL);
    SendMessage(g_hOutputEdit, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);

    HWND hLbl3 = CreateWindowW(L"STATIC", L"命令:",
        WS_CHILD|WS_VISIBLE, 14, 490, 50, 20, hParent, NULL, NULL, NULL);
    SendMessage(hLbl3, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

    g_hCommandEdit = CreateWindowW(L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
        64, 488, 526, 26, hParent, (HMENU)IDC_COMMAND_EDIT, NULL, NULL);
    SendMessage(g_hCommandEdit, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);

    HWND hExec = CreateWindowW(L"BUTTON", L"执行",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
        596, 488, 94, 26, hParent, (HMENU)IDC_EXECUTE_BTN, NULL, NULL);
    SendMessage(hExec, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    g_btnStyles[hExec] = {true, false};
    g_btnHover[hExec] = 0;

    g_adbPageCtrls.push_back(hLbl1);
    g_adbPageCtrls.push_back(g_hDeviceList);
    g_adbPageCtrls.push_back(hLbl2);
    g_adbPageCtrls.push_back(hClear);
    g_adbPageCtrls.push_back(g_hOutputEdit);
    g_adbPageCtrls.push_back(hLbl3);
    g_adbPageCtrls.push_back(g_hCommandEdit);
    g_adbPageCtrls.push_back(hExec);
}

// =========================================================
// 终端页
// =========================================================
void CreateTermPage(HWND hParent) {
    HWND hLbl = CreateWindowW(L"STATIC", L"系统 CMD 终端  (直接输入命令回车执行)",
        WS_CHILD|WS_VISIBLE, 14,10,500,20,hParent,NULL,NULL,NULL);
    SendMessage(hLbl, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
    g_hTermCtrls[0] = hLbl;

    g_hTermOutput = CreateWindowW(L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|WS_HSCROLL|
        ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_READONLY,
        14, 36, 676, 440, hParent, (HMENU)IDC_TERM_OUTPUT, NULL, NULL);
    SendMessage(g_hTermOutput, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
    g_hTermCtrls[1] = g_hTermOutput;

    HWND hLblIn = CreateWindowW(L"STATIC", L">",
        WS_CHILD|WS_VISIBLE, 14, 490, 20, 20, hParent, NULL, NULL, NULL);
    SendMessage(hLblIn, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
    g_hTermCtrls[2] = hLblIn;

    g_hTermInput = CreateWindowW(L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
        36, 488, 554, 26, hParent, (HMENU)IDC_TERM_INPUT, NULL, NULL);
    SendMessage(g_hTermInput, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);
    g_hTermCtrls[3] = g_hTermInput;

    HWND hSend = CreateWindowW(L"BUTTON", L"发送",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
        596, 488, 94, 26, hParent, (HMENU)IDC_TERM_SEND, NULL, NULL);
    SendMessage(hSend, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    g_btnStyles[hSend] = {true, false};
    g_btnHover[hSend] = 0;
    g_hTermCtrls[4] = hSend;
}

// =========================================================
// 主窗口
// =========================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hMainWnd = hWnd;

        g_hFontUI = CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        g_hFontBold = CreateFontW(-14,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
            0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        g_hFontMono = CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            0,0,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");

        g_brBg = CreateSolidBrush(COL_BG);
        g_brPanel = CreateSolidBrush(COL_PANEL);
        g_brInput = CreateSolidBrush(COL_INPUT_BG);

        g_hTab = CreateWindowW(WC_TABCONTROLW, NULL,
            WS_CHILD|WS_VISIBLE|TCS_OWNERDRAWFIXED,
            0, 0, 720, 560, hWnd, (HMENU)IDC_TAB, NULL, NULL);
        SendMessage(g_hTab, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

        TCITEMW tie = {0};
        tie.mask = TCIF_TEXT;
        tie.pszText = (LPWSTR)L"  ADB  ";
        SendMessageW(g_hTab, TCM_INSERTITEMW, 0, (LPARAM)&tie);
        tie.pszText = (LPWSTR)L"  终端  ";
        SendMessageW(g_hTab, TCM_INSERTITEMW, 1, (LPARAM)&tie);

        CreateAdbPage(hWnd);
        CreateTermPage(hWnd);

        g_hStatus = CreateWindowW(L"STATIC", L"  ● 就绪",
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            0, 0, 720, 24, hWnd, (HMENU)IDC_STATUS, NULL, NULL);
        SendMessage(g_hStatus, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

        for (HWND c : g_hTermCtrls) if (c) ShowWindow(c, SW_HIDE);

        StartTerm();
        RefreshDeviceList();

        DoAppendOutput("════════════════════════════════════\r\n");
        DoAppendOutput("  ADB 图形化工具 v3.2\r\n");
        DoAppendOutput("════════════════════════════════════\r\n");
        DoAppendOutput("提示: 请确保 adb.exe 和 fastboot.exe 已在 PATH 中\r\n");
        DoAppendOutput("中文路径/中文 APK 已支持\r\n\r\n");

        g_scrcpyPath = LocateScrcpy();
        if (!g_scrcpyPath.empty())
            DoAppendOutput("[scrcpy] 已找到: " + g_scrcpyPath + "\r\n\r\n");
        else
            DoAppendOutput("[scrcpy] 未找到, 首次使用时会让您指定路径\r\n\r\n");

        SetTimer(hWnd, 1, 1500, NULL);
        break;
    }

    case WM_TIMER:
        if (wParam == 1) UpdateStatusBar();
        else if (wParam == 2) {  // 重启后自动刷新
            KillTimer(hWnd, 2);
            RefreshDeviceList();
        }
        break;

    case WM_APPEND_OUTPUT: {
        char* p = (char*)wParam;
        if (p) { DoAppendOutput(std::string(p)); delete[] p; }
        return 0;
    }
    case WM_APPEND_TERM: {
        char* p = (char*)wParam;
        if (p) { DoAppendTerm(std::string(p)); delete[] p; }
        return 0;
    }
    case WM_DEVICES_READY: {
        struct Payload { std::vector<std::string> devs; std::vector<std::string> sel; };
        std::unique_ptr<Payload> pl((Payload*)wParam);
        if (pl->devs.empty()) {
            SendMessageW(g_hDeviceList, LB_ADDSTRING, 0, (LPARAM)L"(无设备)");
        } else {
            for (auto& d : pl->devs) {
                std::wstring w = U8ToW(d);
                int idx = (int)SendMessageW(g_hDeviceList, LB_ADDSTRING, 0, (LPARAM)w.c_str());
                for (auto& s : pl->sel) if (s == d)
                    SendMessage(g_hDeviceList, LB_SETSEL, TRUE, idx);
            }
        }
        return 0;
    }
    case WM_TERM_DEAD: {
        PostTerm("\r\n[终端进程已退出]\r\n");
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)lParam;
        if (nh->code == TCN_SELCHANGE) {
            int sel = (int)SendMessage(g_hTab, TCM_GETCURSEL, 0, 0);
            for (HWND c : g_adbPageCtrls) if (c) ShowWindow(c, sel == 0 ? SW_SHOW : SW_HIDE);
            for (HWND c : g_hTermCtrls) if (c) ShowWindow(c, sel == 1 ? SW_SHOW : SW_HIDE);
            if (sel == 1 && g_hTermInput) SetFocus(g_hTermInput);
        }
        if (nh->code == NM_CUSTOMDRAW && nh->idFrom == IDC_TAB) {
            LPNMTTCUSTOMDRAW cd = (LPNMTTCUSTOMDRAW)lParam;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(cd->nmcd.hdc, COL_TEXT);
                SetBkColor(cd->nmcd.hdc, COL_PANEL);
                return CDRF_NEWFONT;
            }
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_BG);
        HWND hChild = (HWND)lParam;
        if (hChild == g_hStatus) {
            SetTextColor(hdc, RGB(180,180,180));
            SetBkColor(hdc, COL_PANEL2);
            static HBRUSH brStatus = CreateSolidBrush(COL_PANEL2);
            return (LRESULT)brStatus;
        }
        return (LRESULT)g_brBg;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_INPUT_BG);
        return (LRESULT)g_brInput;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_PANEL);
        return (LRESULT)g_brPanel;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect((HDC)wParam, &rc, g_brBg);
        return 1;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType == ODT_BUTTON) { DrawButton(dis); return TRUE; }
        break;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND hCur = ChildWindowFromPoint(hWnd, pt);
        for (auto& kv : g_btnHover) {
            if (!IsWindow(kv.first)) continue;
            bool now = (kv.first == hCur);
            if ((int)now != kv.second) { kv.second = now; InvalidateRect(kv.first, NULL, TRUE); }
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_REFRESH_BTN: RefreshDeviceList(); break;

        case IDC_EXECUTE_BTN: {
            wchar_t buf[1024] = {0};
            GetWindowTextW(g_hCommandEdit, buf, 1023);
            std::string cmd = WToU8(buf);
            if (cmd.empty()) break;
            SetWindowTextW(g_hCommandEdit, L"");
            if (g_shell.running.load()) {
                PostOutput("> " + cmd + "\r\n");
                WriteShell(cmd);
            } else {
                RunForEachSelected(cmd);
            }
            break;
        }
        case IDC_CLEAR_BTN: SetWindowTextW(g_hOutputEdit, L""); break;

        case IDC_TERM_SEND: {
            wchar_t buf[1024] = {0};
            GetWindowTextW(g_hTermInput, buf, 1023);
            std::string cmd = WToU8(buf);
            if (cmd.empty()) break;
            SetWindowTextW(g_hTermInput, L"");
            if (!g_term.running.load()) { StartTerm(); Sleep(100); }
            WriteTerm(cmd);
            break;
        }
        case IDC_TERM_CLEAR: SetWindowTextW(g_hTermOutput, L""); break;
        case IDC_TERM_RESTART: StartTerm(); break;

        case IDC_INSTALL_BTN: InstallApkSmart(hWnd); break;

        case IDC_SCREENSHOT_BTN: {
            auto devs = GetSelectedDevices();
            if (devs.empty()) { PostOutput("[错误] 未选中设备\r\n"); break; }
            wchar_t desktop[MAX_PATH];
            SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop);
            std::string deskPath = WToU8(desktop);
            SYSTEMTIME st; GetLocalTime(&st);
            char ts[64];
            sprintf(ts, "%04d%02d%02d_%02d%02d%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            for (size_t i = 0; i < devs.size(); i++) {
                std::string remote = "/sdcard/_scr_" + std::string(ts) + ".png";
                char nameBuf[128];
                sprintf(nameBuf, "\\screenshot_%s_%zu.png", ts, i);
                std::string local = deskPath + nameBuf;
                std::string d = devs[i];
                std::thread([d, remote, local]() {
                    RunStreaming("adb -s " + d + " shell screencap -p " + remote,
                                 [](const std::string& s){ PostOutput(s); });
                    RunStreaming("adb -s " + d + " pull " + remote + " \"" + local + "\"",
                                 [](const std::string& s){ PostOutput(s); });
                    RunStreaming("adb -s " + d + " shell rm " + remote,
                                 [](const std::string& s){ PostOutput(s); });
                    PostOutput("[截图完成] " + local + "\r\n");
                }).detach();
            }
            break;
        }
        case IDC_SHELL_BTN: {
            if (g_shell.running.load()) {
                StopShell();
                SetWindowTextW(g_hShellBtn, L"进入 adb Shell");
            } else {
                std::string dev = GetSingleDevice();
                if (dev.empty()) { PostOutput("[错误] 未选中设备\r\n"); break; }
                if (IsDeviceFastboot(dev)) {
                    PostOutput("[错误] fastboot 设备不支持 adb shell\r\n");
                    break;
                }
                if (StartShell(dev))
                    SetWindowTextW(g_hShellBtn, L"退出 Shell");
            }
            break;
        }
        case IDC_LOGCAT_BTN: {
            if (g_logcatRunning.load()) StopLogcat();
            else {
                std::string dev = GetSingleDevice();
                if (dev.empty()) { PostOutput("[错误] 未选中设备\r\n"); break; }
                if (IsDeviceFastboot(dev)) {
                    PostOutput("[错误] fastboot 设备不支持 logcat\r\n");
                    break;
                }
                StartLogcat(dev);
            }
            break;
        }
        case IDC_SCRCPY_BTN: {
            auto devs = GetSelectedDevices();
            if (devs.empty()) { PostOutput("[错误] 请先选择一台设备\r\n"); break; }
            if (IsDeviceFastboot(devs[0])) {
                PostOutput("[错误] fastboot 设备不支持 scrcpy 投屏\r\n");
                MessageBoxW(hWnd, L"scrcpy 只能投屏 adb 模式下的设备。\n请先进入系统。",
                            L"错误", MB_ICONWARNING);
                break;
            }
            if (devs.size() > 1)
                PostOutput("[提示] 选中了多台设备, 只投第一台\r\n");
            StartScrcpy(devs[0]);
            break;
        }
        case IDC_SCRCPY_SET: {
            std::string picked = PromptForScrcpy(hWnd);
            if (!picked.empty()) {
                g_scrcpyPath = picked;
                PostOutput("[已设置 scrcpy 路径] " + picked + "\r\n");
            }
            break;
        }
        case IDC_REBOOT_BTN: {
            if (MessageBoxW(hWnd, L"确定重启选中设备?", L"确认", MB_YESNO) == IDYES) {
                auto devs = GetSelectedDevices();
                if (devs.empty()) { PostOutput("[错误] 未选中设备\r\n"); break; }
                for (auto& d : devs) {
                    bool fb = IsDeviceFastboot(d);
                    std::string cmd = fb
                        ? "fastboot -s " + d + " reboot"
                        : "adb -s " + d + " reboot";
                    PostOutput("\r\n[CMD] " + cmd + "\r\n");
                    RunStreaming(cmd,
                        [](const std::string& s){ PostOutput(s); },
                        [](){ PostOutput("[已发出重启指令, 设备将断连]\r\n"); },
                        3000);
                }
                SetTimer(hWnd, 2, 5000, NULL);
            }
            break;
        }
        case IDC_REBOOT_RECOV: {
            if (MessageBoxW(hWnd, L"重启到 Recovery 模式?", L"确认", MB_YESNO) == IDYES) {
                auto devs = GetSelectedDevices();
                if (devs.empty()) break;
                for (auto& d : devs) {
                    bool fb = IsDeviceFastboot(d);
                    std::string cmd = fb
                        ? "fastboot -s " + d + " reboot-recovery"
                        : "adb -s " + d + " reboot recovery";
                    PostOutput("\r\n[CMD] " + cmd + "\r\n");
                    RunStreaming(cmd,
                        [](const std::string& s){ PostOutput(s); },
                        [](){ PostOutput("[已发出重启指令]\r\n"); },
                        3000);
                }
                SetTimer(hWnd, 2, 5000, NULL);
            }
            break;
        }
        case IDC_REBOOT_BOOT: {
            if (MessageBoxW(hWnd, L"重启到 Fastboot 模式?", L"确认", MB_YESNO) == IDYES) {
                auto devs = GetSelectedDevices();
                if (devs.empty()) break;
                for (auto& d : devs) {
                    bool fb = IsDeviceFastboot(d);
                    if (fb) {
                        PostOutput("[提示] 该设备已在 fastboot 模式\r\n");
                        continue;
                    }
                    std::string cmd = "adb -s " + d + " reboot bootloader";
                    PostOutput("\r\n[CMD] " + cmd + "\r\n");
                    RunStreaming(cmd,
                        [](const std::string& s){ PostOutput(s); },
                        [](){ PostOutput("[已发出重启指令, 稍后刷新可看到 fastboot 设备]\r\n"); },
                        3000);
                }
                SetTimer(hWnd, 2, 5000, NULL);
            }
            break;
        }
        case IDC_INFO_BTN: {
            auto devs = GetSelectedDevices();
            if (devs.empty()) { PostOutput("[错误] 未选中设备\r\n"); break; }
            for (auto& d : devs) {
                if (IsDeviceFastboot(d)) {
                    PostOutput("\r\n──── fastboot 设备信息: " + d + " ────\r\n");
                    std::thread([d]() {
                        RunStreaming("fastboot -s " + d + " getvar all",
                                     [](const std::string& s){ PostOutput(s); });
                    }).detach();
                    continue;
                }
                PostOutput("\r\n──── 设备信息: " + d + " ────\r\n");
                std::thread([d]() {
                    RunStreaming("adb -s " + d + " shell getprop ro.product.model",
                                 [](const std::string& s){ PostOutput("型号:    " + s); });
                    RunStreaming("adb -s " + d + " shell getprop ro.build.version.release",
                                 [](const std::string& s){ PostOutput("Android: " + s); });
                    RunStreaming("adb -s " + d + " shell getprop ro.build.version.sdk",
                                 [](const std::string& s){ PostOutput("SDK:     " + s); });
                    RunStreaming("adb -s " + d + " shell getprop ro.product.brand",
                                 [](const std::string& s){ PostOutput("品牌:    " + s); });
                    RunStreaming("adb -s " + d + " shell getprop ro.product.cpu.abi",
                                 [](const std::string& s){ PostOutput("CPU:     " + s); });
                }).detach();
            }
            break;
        }
        case IDC_BATTERY_BTN: RunForEachSelected("shell dumpsys battery"); break;

        case IDC_WIRELESS_BTN: {
            std::wstring tips =
                L"无线 ADB 操作步骤:\r\n"
                L"1) USB 连接后, 点击\"是\"执行 adb tcpip 5555\r\n"
                L"2) 拔掉 USB 线, 找到手机 IP (设置-关于手机-状态)\r\n"
                L"3) 在本工具命令框输入: connect <手机IP>:5555\r\n\r\n"
                L"是否立即对选中设备执行 adb tcpip 5555 ?";
            if (MessageBoxW(hWnd, tips.c_str(), L"无线 ADB", MB_YESNO) == IDYES)
                RunForEachSelected("tcpip 5555");
            break;
        }
        case IDC_FILES_BTN: ShowFileManager(); break;
        case IDC_PROCS_BTN: ShowProcessManager(); break;
        case IDC_APPS_BTN:  ShowAppManager(); break;
        }
        break;

    case WM_SIZE: {
        RECT rc; GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (g_hTab) SetWindowPos(g_hTab, NULL, 0, 0, w, h-24, SWP_NOZORDER);
        if (g_hStatus) SetWindowPos(g_hStatus, NULL, 0, h-24, w, 24, SWP_NOZORDER);
        if (g_hOutputEdit)
            SetWindowPos(g_hOutputEdit, NULL, 14, 238, w-28, h-24-238-46, SWP_NOZORDER);
        if (g_hCommandEdit)
            SetWindowPos(g_hCommandEdit, NULL, 64, h-24-46, w-64-124, 26, SWP_NOZORDER);
        if (g_hTermOutput)
            SetWindowPos(g_hTermOutput, NULL, 14, 36, w-28, h-24-36-46, SWP_NOZORDER);
        if (g_hTermInput)
            SetWindowPos(g_hTermInput, NULL, 36, h-24-46, w-36-124, 26, SWP_NOZORDER);
        HWND hExec = GetDlgItem(hWnd, IDC_EXECUTE_BTN);
        if (hExec) SetWindowPos(hExec, NULL, w-110, h-24-46, 94, 26, SWP_NOZORDER);
        HWND hSend = GetDlgItem(hWnd, IDC_TERM_SEND);
        if (hSend) SetWindowPos(hSend, NULL, w-110, h-24-46, 94, 26, SWP_NOZORDER);
        HWND hClear = GetDlgItem(hWnd, IDC_CLEAR_BTN);
        if (hClear) SetWindowPos(hClear, NULL, w-110, 210, 100, 26, SWP_NOZORDER);
        break;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 740;
        mmi->ptMinTrackSize.y = 580;
        break;
    }

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        KillTimer(hWnd, 2);
        StopShell();
        StopLogcat();
        StopTerm();
        if (g_scrcpyRunning.load() && g_scrcpyPi.hProcess)
            TerminateProcess(g_scrcpyPi.hProcess, 0);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// =========================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex{ sizeof(icex), ICC_WIN95_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"AdbGuiToolClassV32";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"注册窗口类失败", L"错误", MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowExW(0, L"AdbGuiToolClassV32",
        L"ADB 图形化工具 v3.2",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 740, 620,
        NULL, NULL, hInstance, NULL);
    if (!hWnd) return 1;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}