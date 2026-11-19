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
    ConsoleOutputCallback onOutput;
    void *context;
};

static DWORD WINAPI ConsoleReader(LPVOID parameter) {
    ConsoleProcess *console = (ConsoleProcess *)parameter;
    char buffer[2048];
    DWORD bytesRead;

    while (ReadFile(console->outputRead, buffer, sizeof(buffer), &bytesRead, NULL) &&
           bytesRead > 0) {
        if (console->onOutput)
            console->onOutput(buffer, bytesRead, console->context);
    }
    return 0;
}

ConsoleProcess *ConsoleStart(ConsoleOutputCallback onOutput, void *context) {
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
    console->onOutput = onOutput;
    console->context = context;

    console->readerThread = CreateThread(NULL, 0, ConsoleReader, console, 0, NULL);
    if (!console->readerThread) {
        ConsoleStop(console);
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

void ConsoleStop(ConsoleProcess *console) {
    if (!console) return;

    if (console->inputWrite) {
        CloseHandle(console->inputWrite);
        console->inputWrite = NULL;
    }

    if (console->process) {
        DWORD waitResult = WaitForSingleObject(console->process, 1000);
        if (waitResult == WAIT_TIMEOUT) {
            TerminateProcess(console->process, 0);
            WaitForSingleObject(console->process, 1000);
        }
    }

    if (console->readerThread) {
        WaitForSingleObject(console->readerThread, 1500);
        CloseHandle(console->readerThread);
    }

    if (console->outputRead) CloseHandle(console->outputRead);
    if (console->thread) CloseHandle(console->thread);
    if (console->process) CloseHandle(console->process);
    free(console);
}
