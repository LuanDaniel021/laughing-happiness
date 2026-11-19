#ifndef TERMINAL_CONSOLE_H
#define TERMINAL_CONSOLE_H

#include <windows.h>

typedef void (*ConsoleOutputCallback)(const char *data, DWORD length, void *context);

typedef struct ConsoleProcess ConsoleProcess;

/* Starts cmd.exe with redirected standard input/output. */
ConsoleProcess *ConsoleStart(ConsoleOutputCallback onOutput, void *context);

/* Writes bytes to the interpreter's standard input. */
BOOL ConsoleWrite(ConsoleProcess *console, const char *data, DWORD length);

/* Stops the child process and releases all resources. */
void ConsoleStop(ConsoleProcess *console);

#endif
