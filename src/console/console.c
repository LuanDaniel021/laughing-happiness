#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "console.h"

struct ConsoleProcess {
    HANDLE process;
    HANDLE thread;
    HANDLE inputWrite;
    HANDLE outputRead;
    HANDLE readerThread;
    ConsoleEventCallback onEvent;
    void *context;
};

static void EmitEvent(ConsoleProcess *console, ConsoleEventType type,
                      const char *data, DWORD length, DWORD exitCode) {
    ConsoleEvent event;
    if (!console->onEvent) return;

    event.type = type;
    event.data = data;
    event.length = length;
    event.exitCode = exitCode;
    event.foreground = 0;
    event.background = 0;
    console->onEvent(&event, console->context);
}

static DWORD WINAPI ConsoleReader(LPVOID parameter) {
    ConsoleProcess *console = (ConsoleProcess *)parameter;
    char buffer[2048];
    DWORD bytesRead;

    while (ReadFile(console->outputRead, buffer, sizeof(buffer), &bytesRead, NULL) &&
           bytesRead > 0) {
        EmitEvent(console, CONSOLE_EVENT_OUTPUT, buffer, bytesRead, 0);
    }

    /* EOF means the child closed its output pipe; report process termination. */
    WaitForSingleObject(console->process, INFINITE);
    {
        DWORD exitCode = 0;
        GetExitCodeProcess(console->process, &exitCode);
        EmitEvent(console, CONSOLE_EVENT_EXIT, NULL, 0, exitCode);
    }
    return 0;
}

ConsoleProcess *ConsoleStart(ConsoleEventCallback onEvent, void *context) {
    SECURITY_ATTRIBUTES security;
    STARTUPINFOA startup;
    PROCESS_INFORMATION processInfo;
    HANDLE childInputRead = NULL;
    HANDLE childOutputWrite = NULL;
    HANDLE parentInputWrite = NULL;
    HANDLE parentOutputRead = NULL;
    char systemDirectory[MAX_PATH];
    char commandLine[MAX_PATH + 32];
    UINT systemLength;
    ConsoleProcess *console = NULL;

    ZeroMemory(&security, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    if (!CreatePipe(&childInputRead, &parentInputWrite, &security, 0))
        goto failure;
    if (!SetHandleInformation(parentInputWrite, HANDLE_FLAG_INHERIT, 0))
        goto failure;

    if (!CreatePipe(&parentOutputRead, &childOutputWrite, &security, 0))
        goto failure;
    if (!SetHandleInformation(parentOutputRead, HANDLE_FLAG_INHERIT, 0))
        goto failure;

    systemLength = GetSystemDirectoryA(systemDirectory, MAX_PATH);
    if (systemLength == 0 || systemLength >= MAX_PATH)
        goto failure;

    if (wsprintfA(commandLine, "\"%s\\cmd.exe\" /Q /D /K", systemDirectory) == 0)
        goto failure;

    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInputRead;
    startup.hStdOutput = childOutputWrite;
    startup.hStdError = childOutputWrite;

    ZeroMemory(&processInfo, sizeof(processInfo));
    if (!CreateProcessA(NULL, commandLine, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &startup, &processInfo))
        goto failure;

    CloseHandle(childInputRead);
    childInputRead = NULL;
    CloseHandle(childOutputWrite);
    childOutputWrite = NULL;

    console = (ConsoleProcess *)calloc(1, sizeof(ConsoleProcess));
    if (!console) {
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        goto failure;
    }

    console->process = processInfo.hProcess;
    console->thread = processInfo.hThread;
    console->inputWrite = parentInputWrite;
    console->outputRead = parentOutputRead;
    console->onEvent = onEvent;
    console->context = context;

    console->readerThread = CreateThread(NULL, 0, ConsoleReader, console, 0, NULL);
    if (!console->readerThread) {
        TerminateProcess(console->process, 1);
        WaitForSingleObject(console->process, INFINITE);
        CloseHandle(console->inputWrite);
        CloseHandle(console->outputRead);
        CloseHandle(console->thread);
        CloseHandle(console->process);
        free(console);
        return NULL;
    }

    return console;

failure:
    if (childInputRead) CloseHandle(childInputRead);
    if (childOutputWrite) CloseHandle(childOutputWrite);
    if (parentInputWrite) CloseHandle(parentInputWrite);
    if (parentOutputRead) CloseHandle(parentOutputRead);
    return NULL;
}

BOOL ConsoleWrite(ConsoleProcess *console, const char *data, DWORD length) {
    DWORD totalWritten = 0;
    if (!console || !console->inputWrite || !data)
        return FALSE;

    while (totalWritten < length) {
        DWORD written = 0;
        if (!WriteFile(console->inputWrite, data + totalWritten,
                       length - totalWritten, &written, NULL) || written == 0)
            return FALSE;
        totalWritten += written;
    }
    return TRUE;
}

void ConsoleSetColor(ConsoleProcess *console, COLORREF foreground, COLORREF background) {
    ConsoleEvent event;
    if (!console || !console->onEvent) return;
    ZeroMemory(&event, sizeof(event));
    event.type = CONSOLE_EVENT_SET_COLOR;
    event.foreground = foreground;
    event.background = background;
    console->onEvent(&event, console->context);
}

void ConsoleStop(ConsoleProcess *console) {
    if (!console) return;

    if (console->inputWrite) {
        CloseHandle(console->inputWrite);
        console->inputWrite = NULL;
    }

    if (console->process && WaitForSingleObject(console->process, 1000) == WAIT_TIMEOUT) {
        TerminateProcess(console->process, 0);
        WaitForSingleObject(console->process, INFINITE);
    }

    if (console->readerThread) {
        WaitForSingleObject(console->readerThread, INFINITE);
        CloseHandle(console->readerThread);
    }

    if (console->outputRead) CloseHandle(console->outputRead);
    if (console->thread) CloseHandle(console->thread);
    if (console->process) CloseHandle(console->process);
    free(console);
}
