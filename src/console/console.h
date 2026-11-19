#ifndef TERMINAL_CONSOLE_H
#define TERMINAL_CONSOLE_H

#include <windows.h>

typedef enum {
    CONSOLE_EVENT_OUTPUT,
    CONSOLE_EVENT_CLEAR_SCREEN,
    CONSOLE_EVENT_SET_COLOR,
    CONSOLE_EVENT_EXIT
} ConsoleEventType;

typedef struct {
    ConsoleEventType type;
    const char *data;
    DWORD length;
    DWORD exitCode;
    COLORREF foreground;
    COLORREF background;
} ConsoleEvent;

typedef void (*ConsoleEventCallback)(const ConsoleEvent *event, void *context);

typedef struct ConsoleProcess ConsoleProcess;

/* Starts cmd.exe and reports output/control events through one callback. */
ConsoleProcess *ConsoleStart(ConsoleEventCallback onEvent, void *context);

/* Writes bytes to the interpreter's standard input. */
BOOL ConsoleWrite(ConsoleProcess *console, const char *data, DWORD length);

/* Reports a terminal color change through the event callback. */
void ConsoleSetColor(ConsoleProcess *console, COLORREF foreground, COLORREF background);

/* Stops the child process and releases all resources. */
void ConsoleStop(ConsoleProcess *console);

#endif
