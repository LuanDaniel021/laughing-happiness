#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "console/console.h"

#define MAX_ROWS 2048
#define MAX_COLS 300
#define INPUT_SIZE 512
#define TIMER_CURSOR 1
#define WM_CONSOLE_EVENT (WM_APP + 1)

typedef struct {
    ConsoleEventType type;
    DWORD length;
    DWORD exitCode;
    COLORREF foreground;
    COLORREF background;
    char data[2048];
} ConsoleMessage;

typedef struct {
    char ch;
    COLORREF color;
} Cell;

static Cell screenBuffer[MAX_ROWS][MAX_COLS];
static int usedRows = 1;
static int cursorRow = 0, cursorCol = 0;
static int columns = 80, viewRows = 25, scrollTop = 0;
static int cursorVisible = 1, followOutput = 1;
static int cellWidth = 9, cellHeight = 18;
static int inputStartRow = 0, inputStartCol = 0;
static char input[INPUT_SIZE];
static int inputLength = 0;
static const COLORREF defaultForeground = RGB(220, 220, 220);
static const COLORREF defaultBackground = RGB(15, 17, 20);
static COLORREF currentColor = RGB(220, 220, 220);
static COLORREF backgroundColor = RGB(15, 17, 20);
static int ansiState = 0;
static char ansiParameters[64];
static int ansiParameterLength = 0;
static HWND mainWindow;
static HFONT terminalFont;
static ConsoleProcess *consoleProcess;

static void UpdateScroll(void);
static void InvalidateTerminal(void);

static void EnsureRow(int row) {
    if (row >= MAX_ROWS) row = MAX_ROWS - 1;
    while (usedRows <= row) {
        memset(screenBuffer[usedRows], 0, sizeof(screenBuffer[usedRows]));
        usedRows++;
    }
}

static void NewLine(void) {
    cursorCol = 0;
    if (cursorRow < MAX_ROWS - 1) cursorRow++;
    EnsureRow(cursorRow);
}

static void ClearBuffer(void) {
    memset(screenBuffer, 0, sizeof(screenBuffer));
    usedRows = 1;
    cursorRow = cursorCol = scrollTop = 0;
    followOutput = 1;
}

static void PutChar(char ch, COLORREF color) {
    if (ch == '\r') return;
    if (ch == '\f') {
        ClearBuffer();
        return;
    }
    if (ch == '\n') {
        NewLine();
        return;
    }
    if (ch == '\0') return;
    if (cursorCol >= columns) NewLine();
    EnsureRow(cursorRow);
    screenBuffer[cursorRow][cursorCol].ch = ch;
    screenBuffer[cursorRow][cursorCol].color = color;
    cursorCol++;
}

static void WriteText(const char *text, COLORREF color) {
    while (*text) PutChar(*text++, color);
    UpdateScroll();
}

static void WriteLine(const char *text, COLORREF color) {
    WriteText(text, color);
    NewLine();
    UpdateScroll();
}

static void PrintPrompt(void) {
    WriteText(">", RGB(90, 190, 255));
    WriteText(" ", currentColor);
    inputStartRow = cursorRow;
    inputStartCol = cursorCol;
    inputLength = 0;
    input[0] = '\0';
    UpdateScroll();
}


static int HexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static COLORREF CmdColor(int value) {
    static const COLORREF colors[16] = {
        RGB(0,0,0), RGB(0,0,128), RGB(0,128,0), RGB(0,128,128),
        RGB(128,0,0), RGB(128,0,128), RGB(128,128,0), RGB(192,192,192),
        RGB(128,128,128), RGB(0,0,255), RGB(0,255,0), RGB(0,255,255),
        RGB(255,0,0), RGB(255,0,255), RGB(255,255,0), RGB(255,255,255)
    };
    return colors[value & 15];
}

/* CMD color is emulated because redirected CMD has no console attributes. */
static BOOL TryHandleColorCommand(const char *command) {
    const char *arg = command;
    int background, foreground;

    while (*arg == ' ' || *arg == '\t') arg++;
    if (_strnicmp(arg, "color", 5) != 0) return FALSE;
    arg += 5;
    if (*arg && *arg != ' ' && *arg != '\t') return FALSE;
    while (*arg == ' ' || *arg == '\t') arg++;

    if (!*arg) {
        ConsoleSetColor(consoleProcess, defaultForeground, defaultBackground);
        return TRUE;
    }

    if (arg[0] == '/' || !arg[0] || !arg[1] || arg[2]) return FALSE;
    background = HexDigit(arg[0]);
    foreground = HexDigit(arg[1]);
    if (background < 0 || foreground < 0 || background == foreground)
        return FALSE;

    ConsoleSetColor(consoleProcess, CmdColor(foreground), CmdColor(background));
    return TRUE;
}

static COLORREF AnsiColor(int index) {
    static const COLORREF colors[16] = {
        RGB(0,0,0), RGB(170,0,0), RGB(0,170,0), RGB(170,85,0),
        RGB(0,0,170), RGB(170,0,170), RGB(0,170,170), RGB(170,170,170),
        RGB(85,85,85), RGB(255,85,85), RGB(85,255,85), RGB(255,255,85),
        RGB(85,85,255), RGB(255,85,255), RGB(85,255,255), RGB(255,255,255)
    };
    return colors[index & 15];
}

static void ApplyAnsiSgr(void) {
    int values[16], count = 0;
    char *cursor = ansiParameters;

    if (!*cursor) values[count++] = 0;
    while (*cursor && count < 16) {
        char *end;
        long value = strtol(cursor, &end, 10);
        if (end == cursor) break;
        values[count++] = (int)value;
        cursor = end;
        if (*cursor == ';') cursor++;
        else if (*cursor) break;
    }

    for (int i = 0; i < count; i++) {
        int value = values[i];
        if (value == 0) {
            currentColor = defaultForeground;
            backgroundColor = defaultBackground;
        } else if (value >= 30 && value <= 37) currentColor = AnsiColor(value - 30);
        else if (value >= 90 && value <= 97) currentColor = AnsiColor(value - 90 + 8);
        else if (value == 39) currentColor = defaultForeground;
        else if (value >= 40 && value <= 47) backgroundColor = AnsiColor(value - 40);
        else if (value >= 100 && value <= 107) backgroundColor = AnsiColor(value - 100 + 8);
        else if (value == 49) backgroundColor = defaultBackground;
    }
}

static void HandleOutputByte(unsigned char ch) {
    if (ansiState == 0) {
        if (ch == 0x1B) { ansiState = 1; return; }
        PutChar((char)ch, currentColor);
        return;
    }

    if (ansiState == 1) {
        if (ch == '[') {
            ansiState = 2;
            ansiParameterLength = 0;
            ansiParameters[0] = '\0';
        } else {
            ansiState = 0;
            PutChar((char)ch, currentColor);
        }
        return;
    }

    if (ch >= 0x40 && ch <= 0x7E) {
        ansiParameters[ansiParameterLength] = '\0';
        if (ch == 'm') ApplyAnsiSgr();
        else if (ch == 'J' && strcmp(ansiParameters, "2") == 0) ClearBuffer();
        ansiState = 0;
        ansiParameterLength = 0;
        return;
    }

    if (ansiParameterLength < (int)sizeof(ansiParameters) - 1)
        ansiParameters[ansiParameterLength++] = (char)ch;
    else {
        ansiState = 0;
        ansiParameterLength = 0;
    }
}

static void SendCommand(void) {
    input[inputLength] = '\0';

    /* CMD console attributes are emulated by the terminal when using pipes. */
    if (_stricmp(input, "cls") == 0) {
        ClearBuffer();
        if (consoleProcess) {
            ConsoleWrite(consoleProcess, input, (DWORD)inputLength);
            ConsoleWrite(consoleProcess, "\r\n", 2);
        }
    } else if (TryHandleColorCommand(input)) {
        /* Translated into CONSOLE_EVENT_SET_COLOR. */
    } else if (consoleProcess) {
        ConsoleWrite(consoleProcess, input, (DWORD)inputLength);
        ConsoleWrite(consoleProcess, "\r\n", 2);
    }
    inputLength = 0;
    input[0] = '\0';
    UpdateScroll();
}

static void OnConsoleEvent(const ConsoleEvent *event, void *context) {
    (void)context;

    if (event->type == CONSOLE_EVENT_OUTPUT) {
        DWORD offset = 0;
        while (offset < event->length) {
            DWORD chunkLength = event->length - offset;
            if (chunkLength > 2048) chunkLength = 2048;

            ConsoleMessage *message = (ConsoleMessage *)calloc(1, sizeof(ConsoleMessage));
            if (!message) return;
            message->type = CONSOLE_EVENT_OUTPUT;
            message->length = chunkLength;
            memcpy(message->data, event->data + offset, chunkLength);

            if (!PostMessageA(mainWindow, WM_CONSOLE_EVENT, 0, (LPARAM)message))
                free(message);
            offset += chunkLength;
        }
        return;
    }

    {
        ConsoleMessage *message = (ConsoleMessage *)calloc(1, sizeof(ConsoleMessage));
        if (!message) return;
        message->type = event->type;
        message->exitCode = event->exitCode;
        message->foreground = event->foreground;
        message->background = event->background;

        if (!PostMessageA(mainWindow, WM_CONSOLE_EVENT, 0, (LPARAM)message))
            free(message);
    }
}

static void UpdateScroll(void) {
    if (!mainWindow) return;

    int maxScroll = usedRows - viewRows;
    if (maxScroll < 0) maxScroll = 0;
    if (followOutput) scrollTop = maxScroll;
    if (scrollTop > maxScroll) scrollTop = maxScroll;
    if (scrollTop < 0) scrollTop = 0;

    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = usedRows - 1;
    si.nPage = (UINT)viewRows;
    si.nPos = scrollTop;
    SetScrollInfo(mainWindow, SB_VERT, &si, TRUE);
    InvalidateTerminal();
}

static void InvalidateTerminal(void) {
    if (mainWindow) InvalidateRect(mainWindow, NULL, FALSE);
}

static void DrawTerminal(HDC dc, RECT *client) {
    HBRUSH bg = CreateSolidBrush(backgroundColor);
    FillRect(dc, client, bg);
    DeleteObject(bg);

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, terminalFont);

    for (int row = 0; row < viewRows; row++) {
        int sourceRow = scrollTop + row;
        if (sourceRow >= usedRows) break;

        for (int col = 0; col < columns; col++) {
            Cell *cell = &screenBuffer[sourceRow][col];
            if (!cell->ch) continue;
            SetTextColor(dc, cell->color);
            TextOutA(dc, col * cellWidth, row * cellHeight, &cell->ch, 1);
        }
    }

    if (cursorVisible && cursorRow >= scrollTop &&
        cursorRow < scrollTop + viewRows) {
        int x = cursorCol * cellWidth;
        int y = (cursorRow - scrollTop) * cellHeight;
        RECT caret = { x, y, x + cellWidth, y + cellHeight };
        HBRUSH brush = CreateSolidBrush(RGB(100, 220, 140));
        FillRect(dc, &caret, brush);
        DeleteObject(brush);

        if (cursorCol < columns) {
            char ch = screenBuffer[cursorRow][cursorCol].ch;
            if (ch) {
                SetTextColor(dc, backgroundColor);
                TextOutA(dc, x, y, &ch, 1);
            }
        }
    }
}

static void ResizeTerminal(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC dc = GetDC(hwnd);
    TEXTMETRICA tm;
    SelectObject(dc, terminalFont);
    GetTextMetricsA(dc, &tm);
    cellWidth = tm.tmAveCharWidth;
    cellHeight = tm.tmHeight + tm.tmExternalLeading;
    ReleaseDC(hwnd, dc);

    if (cellWidth < 1) cellWidth = 9;
    if (cellHeight < 1) cellHeight = 18;

    columns = (rc.right - 12) / cellWidth;
    viewRows = (rc.bottom - 4) / cellHeight;
    if (columns < 1) columns = 1;
    if (columns > MAX_COLS) columns = MAX_COLS;
    if (viewRows < 1) viewRows = 1;
    UpdateScroll();
}

static void ProcessCharacter(char ch) {
    if (ch == '\r' || ch == '\n') {
        NewLine();
        SendCommand();
        return;
    }

    if (ch == '\b') {
        if (inputLength > 0) {
            inputLength--;
            input[inputLength] = '\0';

            if (cursorCol > 0) {
                cursorCol--;
            } else if (cursorRow > inputStartRow) {
                cursorRow--;
                cursorCol = columns - 1;
            }

            if (cursorRow >= 0 && cursorCol >= 0) {
                screenBuffer[cursorRow][cursorCol].ch = 0;
                screenBuffer[cursorRow][cursorCol].color = 0;
            }
            UpdateScroll();
        }
        return;
    }

    if ((unsigned char)ch >= 32 && (unsigned char)ch < 127 &&
        inputLength < INPUT_SIZE - 1) {
        if (inputLength == 0) {
            inputStartRow = cursorRow;
            inputStartCol = cursorCol;
        }
        input[inputLength++] = ch;
        input[inputLength] = '\0';
        PutChar(ch, currentColor);
        UpdateScroll();
    }
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, TIMER_CURSOR, 500, NULL);
        return 0;

    case WM_SIZE:
        ResizeTerminal(hwnd);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_CURSOR) {
            cursorVisible = !cursorVisible;
            InvalidateTerminal();
        }
        return 0;

    case WM_CHAR:
        ProcessCharacter((char)wParam);
        return 0;

    case WM_CONSOLE_EVENT: {
        ConsoleMessage *message = (ConsoleMessage *)lParam;
        if (message) {
            switch (message->type) {
            case CONSOLE_EVENT_OUTPUT:
                for (DWORD i = 0; i < message->length; i++)
                    HandleOutputByte((unsigned char)message->data[i]);
                UpdateScroll();
                break;

            case CONSOLE_EVENT_CLEAR_SCREEN:
                ClearBuffer();
                UpdateScroll();
                break;

            case CONSOLE_EVENT_SET_COLOR:
                currentColor = message->foreground;
                backgroundColor = message->background;
                UpdateScroll();
                break;

            case CONSOLE_EVENT_EXIT:
                free(message);
                DestroyWindow(hwnd);
                return 0;
            }
            free(message);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_UP || wParam == VK_PRIOR) {
            SCROLLINFO si = { sizeof(si), SIF_ALL };
            GetScrollInfo(hwnd, SB_VERT, &si);
            int step = (wParam == VK_PRIOR) ? viewRows : 1;
            scrollTop -= step;
            if (scrollTop < 0) scrollTop = 0;
            followOutput = 0;
            UpdateScroll();
        } else if (wParam == VK_DOWN || wParam == VK_NEXT) {
            int step = (wParam == VK_NEXT) ? viewRows : 1;
            scrollTop += step;
            int maxScroll = usedRows - viewRows;
            if (maxScroll < 0) maxScroll = 0;
            if (scrollTop > maxScroll) scrollTop = maxScroll;
            followOutput = (scrollTop == maxScroll);
            UpdateScroll();
        } else if (wParam == VK_END) {
            followOutput = 1;
            UpdateScroll();
        }
        return 0;

    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(hwnd, SB_VERT, &si);
        int pos = scrollTop;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: pos--; break;
        case SB_LINEDOWN: pos++; break;
        case SB_PAGEUP: pos -= viewRows; break;
        case SB_PAGEDOWN: pos += viewRows; break;
        case SB_THUMBTRACK: pos = si.nTrackPos; break;
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = usedRows - viewRows; break;
        }
        int maxScroll = usedRows - viewRows;
        if (maxScroll < 0) maxScroll = 0;
        if (pos < 0) pos = 0;
        if (pos > maxScroll) pos = maxScroll;
        scrollTop = pos;
        followOutput = (scrollTop == maxScroll);
        UpdateScroll();
        return 0;
    }

    case WM_MOUSEWHEEL:
        scrollTop -= GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 3;
        if (scrollTop < 0) scrollTop = 0;
        followOutput = 0;
        UpdateScroll();
        return 0;

    case WM_SETFOCUS:
        cursorVisible = 1;
        InvalidateTerminal();
        return 0;

    case WM_KILLFOCUS:
        cursorVisible = 0;
        InvalidateTerminal();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC paintDC = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HDC memoryDC = CreateCompatibleDC(paintDC);
        HBITMAP bitmap = CreateCompatibleBitmap(paintDC, rc.right, rc.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memoryDC, bitmap);

        DrawTerminal(memoryDC, &rc);
        BitBlt(paintDC, 0, 0, rc.right, rc.bottom, memoryDC, 0, 0, SRCCOPY);

        SelectObject(memoryDC, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memoryDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_CURSOR);
        if (consoleProcess) {
            ConsoleStop(consoleProcess);
            consoleProcess = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR commandLine, int show) {
    (void)previous;
    (void)commandLine;

    terminalFont = CreateFontA(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas"
    );

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = "NativeTerminalWindow";
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) return 1;

    mainWindow = CreateWindowExA(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        "Native Terminal",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 500,
        NULL, NULL, instance, NULL
    );
    if (!mainWindow) return 1;

    ShowWindow(mainWindow, show);
    UpdateWindow(mainWindow);
    SetFocus(mainWindow);

    ClearBuffer();
    WriteLine("Native Terminal - CMD backend", RGB(100, 210, 140));
    WriteLine("Conectando ao cmd.exe...", RGB(150, 160, 175));
    consoleProcess = ConsoleStart(OnConsoleEvent, NULL);
    if (!consoleProcess)
        WriteLine("Falha ao iniciar cmd.exe.", RGB(255, 100, 100));
    else
        WriteLine("CMD conectado. Digite comandos abaixo.", RGB(100, 210, 140));

    MSG message;
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    if (terminalFont) DeleteObject(terminalFont);
    return 0;
}
