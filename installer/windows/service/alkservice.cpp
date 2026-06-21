// g++ alkservice.cpp -o alkservice.exe -mwindows -static -ladvapi32 -luser32
// -lshell32 安装服务：sc create alkaid0 binPath= "C:\path\to\alkservice.exe"
// start= auto 启动服务：sc start alkaid0 停止服务：sc stop alkaid0

#include <windows.h>
#include <cstring>
#include <iostream>
#pragma comment(lib, "advapi32.lib")

// 服务名称（需与安装时一致）
#define SERVICE_NAME "alkaid0"
#define SERVICE_DISPLAY_NAME "Alkaid0Service"

// 全局停止事件句柄，用于通知服务主循环退出
HANDLE g_hStopEvent = NULL;
// 服务状态句柄
SERVICE_STATUS_HANDLE g_hServiceStatus = NULL;

// -------------------- 原有的权限提升函数 --------------------
const LPCSTR g_Privileges[] = {SE_ASSIGNPRIMARYTOKEN_NAME,
                               SE_INCREASE_QUOTA_NAME, SE_AUDIT_NAME,
                               SE_SHUTDOWN_NAME};
const int g_PrivCount = sizeof(g_Privileges) / sizeof(LPCSTR);

BOOL EnableMultiplePrivileges() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        std::cerr << "OpenProcessToken failed, LastError: " << GetLastError()
                  << std::endl;
        return FALSE;
    }

    DWORD bufSize = sizeof(TOKEN_PRIVILEGES) +
                    (g_PrivCount - 1) * sizeof(LUID_AND_ATTRIBUTES);
    PTOKEN_PRIVILEGES pTp = (PTOKEN_PRIVILEGES)LocalAlloc(LPTR, bufSize);
    if (!pTp) {
        std::cerr << "LocalAlloc memory allocation failed" << std::endl;
        CloseHandle(hToken);
        return FALSE;
    }

    pTp->PrivilegeCount = g_PrivCount;
    for (int i = 0; i < g_PrivCount; i++) {
        LUID luid{};
        if (!LookupPrivilegeValueA(NULL, g_Privileges[i], &luid)) {
            std::cerr << "LookupPrivilegeValueA failed, index " << i
                      << ", LastError: " << GetLastError() << std::endl;
            LocalFree(pTp);
            CloseHandle(hToken);
            return FALSE;
        }
        pTp->Privileges[i].Luid = luid;
        pTp->Privileges[i].Attributes = SE_PRIVILEGE_ENABLED;
    }

    AdjustTokenPrivileges(hToken, FALSE, pTp, bufSize, NULL, NULL);
    DWORD err = GetLastError();

    LocalFree(pTp);
    CloseHandle(hToken);

    if (err != ERROR_SUCCESS) {
        std::cerr << "AdjustTokenPrivileges failed, LastError: " << err
                  << std::endl;
        if (err == ERROR_NOT_ALL_ASSIGNED)
            std::cerr << "WARNING 1300: One or more privileges not present in "
                         "current token. Must run as LocalSystem."
                      << std::endl;
        return FALSE;  // 这里改为返回FALSE，因为作为服务必须全部启用
    }

    std::cout << "Success: All target privileges enabled." << std::endl;
    return TRUE;
}

// -------------------- 原有的启动目标程序函数 --------------------
BOOL LaunchTargetExe() {
    char selfPath[MAX_PATH]{};
    DWORD ret = GetModuleFileNameA(NULL, selfPath, MAX_PATH);
    if (ret == 0 || ret >= MAX_PATH) {
        std::cerr << "GetModuleFileNameA failed to get self path." << std::endl;
        return FALSE;
    }

    // 提取当前 EXE 所在目录（保留尾部反斜杠）
    char dirPath[MAX_PATH];
    strcpy(dirPath, selfPath);
    char* pBackslash = strrchr(dirPath, '\\');
    if (!pBackslash) {
        std::cerr << "Failed to parse working directory from executable path."
                  << std::endl;
        return FALSE;
    }
    *(pBackslash + 1) = '\0';  // 截断到目录，保留 '\'

    // 构造目标 EXE 完整路径（同目录下的 alkaid0.exe）
    char exePath[MAX_PATH];
    strcpy(exePath, dirPath);
    strcat(exePath, "alkaid0.exe");

    // ----- 新增：设置环境变量并创建目录 -----
    // 1. 获取 ProgramData 路径（从环境变量，兼容 MinGW）
    char programData[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("ProgramData", programData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        // 备选硬编码（极少发生）
        strcpy(programData, "C:\\ProgramData");
    }

    // 2. 构造 alkaid0 子目录
    char baseDir[MAX_PATH];
    snprintf(baseDir, MAX_PATH, "%s\\alkaid0", programData);

    // 3. 创建目录（若已存在则忽略）
    if (!CreateDirectoryA(baseDir, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            std::cerr << "CreateDirectoryA failed for " << baseDir
                      << ", LastError: " << err << std::endl;
            // 不返回失败，后续继续（可能已有目录但权限问题，但服务通常具备权限）
        }
    }

    // 4. 构造 config.json 和 log.log 的完整路径
    char configPath[MAX_PATH];
    char logPath[MAX_PATH];
    snprintf(configPath, MAX_PATH, "%s\\config.json", baseDir);
    snprintf(logPath, MAX_PATH, "%s\\log.log", baseDir);

    // 5. 设置环境变量（子进程将继承）
    if (!SetEnvironmentVariableA("ALKAID0_CONFIG_PATH", configPath))
        std::cerr
            << "SetEnvironmentVariable for CONFIG_PATH failed, LastError: "
            << GetLastError() << std::endl;
    if (!SetEnvironmentVariableA("ALKAID0_LOG_PATH", logPath))
        std::cerr << "SetEnvironmentVariable for LOG_PATH failed, LastError: "
                  << GetLastError() << std::endl;

    std::cout << "Preparing to launch: " << exePath << std::endl;
    std::cout << "Config path: " << configPath << std::endl;
    std::cout << "Log path: " << logPath << std::endl;
    // ------------------------------------------

    STARTUPINFOA si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr,
                             exePath,  // 命令行使用完整路径
                             nullptr, nullptr, FALSE, 0, nullptr,
                             dirPath,  // 工作目录设为服务 EXE 所在目录
                             &si, &pi);

    if (!ok) {
        std::cerr << "CreateProcessA failed to launch target, LastError: "
                  << GetLastError() << std::endl;
        return FALSE;
    }

    std::cout << "Success: alkaid0.exe launched, PID = " << pi.dwProcessId
              << std::endl;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

// -------------------- 服务控制处理程序 --------------------
DWORD WINAPI ServiceCtrlHandlerEx(DWORD dwControl,
                                  DWORD dwEventType,
                                  LPVOID lpEventData,
                                  LPVOID lpContext) {
    switch (dwControl) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            // 通知服务主循环退出
            SetEvent(g_hStopEvent);
            // 更新服务状态为停止中
            SERVICE_STATUS ss;
            ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
            ss.dwCurrentState = SERVICE_STOP_PENDING;
            ss.dwControlsAccepted = 0;
            ss.dwWin32ExitCode = 0;
            ss.dwServiceSpecificExitCode = 0;
            ss.dwCheckPoint = 0;
            ss.dwWaitHint = 30000;  // 30秒
            SetServiceStatus(g_hServiceStatus, &ss);
            return NO_ERROR;

        default:
            break;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

// -------------------- 服务主函数 --------------------
VOID WINAPI ServiceMain(DWORD argc, LPSTR* argv) {
    // 注册服务控制处理程序
    g_hServiceStatus =
        RegisterServiceCtrlHandlerExA(SERVICE_NAME, ServiceCtrlHandlerEx, NULL);
    if (!g_hServiceStatus) {
        std::cerr << "RegisterServiceCtrlHandlerEx failed, LastError: "
                  << GetLastError() << std::endl;
        return;
    }

    // 创建停止事件
    g_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_hStopEvent) {
        std::cerr << "CreateEvent failed, LastError: " << GetLastError()
                  << std::endl;
        // 报告服务启动失败
        SERVICE_STATUS ss;
        ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        ss.dwCurrentState = SERVICE_STOPPED;
        ss.dwControlsAccepted = 0;
        ss.dwWin32ExitCode = GetLastError();
        ss.dwServiceSpecificExitCode = 0;
        ss.dwCheckPoint = 0;
        ss.dwWaitHint = 0;
        SetServiceStatus(g_hServiceStatus, &ss);
        return;
    }

    // 先报告服务正在启动
    SERVICE_STATUS ss;
    ss.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    ss.dwCurrentState = SERVICE_START_PENDING;
    ss.dwControlsAccepted = 0;
    ss.dwWin32ExitCode = 0;
    ss.dwServiceSpecificExitCode = 0;
    ss.dwCheckPoint = 0;
    ss.dwWaitHint = 30000;
    SetServiceStatus(g_hServiceStatus, &ss);

    // 执行原有的初始化工作：启用特权 + 启动目标程序
    BOOL bInitOk = TRUE;
    if (!EnableMultiplePrivileges()) {
        std::cerr << "Fatal: Privilege activation failed." << std::endl;
        bInitOk = FALSE;
    } else if (!LaunchTargetExe()) {
        std::cerr << "Fatal: Cannot start alkaid0.exe." << std::endl;
        bInitOk = FALSE;
    }

    if (!bInitOk) {
        // 初始化失败，报告停止状态
        ss.dwCurrentState = SERVICE_STOPPED;
        ss.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
        ss.dwServiceSpecificExitCode = 1;
        SetServiceStatus(g_hServiceStatus, &ss);
        CloseHandle(g_hStopEvent);
        return;
    }

    // 初始化成功，报告服务正在运行
    ss.dwCurrentState = SERVICE_RUNNING;
    ss.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    ss.dwWin32ExitCode = 0;
    ss.dwServiceSpecificExitCode = 0;
    ss.dwCheckPoint = 0;
    ss.dwWaitHint = 0;
    SetServiceStatus(g_hServiceStatus, &ss);

    // 主循环：等待停止事件
    WaitForSingleObject(g_hStopEvent, INFINITE);

    // 服务停止
    ss.dwCurrentState = SERVICE_STOPPED;
    ss.dwControlsAccepted = 0;
    ss.dwWin32ExitCode = 0;
    ss.dwServiceSpecificExitCode = 0;
    SetServiceStatus(g_hServiceStatus, &ss);

    CloseHandle(g_hStopEvent);
}

// -------------------- 程序入口 --------------------
int main() {
    // 服务调度表
    SERVICE_TABLE_ENTRYA serviceTable[] = {
        {(LPSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONA)ServiceMain},
        {NULL, NULL}};

    // 启动服务控制调度器
    if (!StartServiceCtrlDispatcherA(serviceTable)) {
        std::cerr << "StartServiceCtrlDispatcher failed, LastError: "
                  << GetLastError() << std::endl;
        // 如果以控制台方式运行（非服务模式），可提示用法
        std::cerr << "This program is designed to run as a Windows service."
                  << std::endl;
        std::cerr << "Please install and start it via 'sc' or Services MMC."
                  << std::endl;
        return 1;
    }

    return 0;
}