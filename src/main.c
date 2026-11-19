#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define MAX_ROWS 2048
#define MAX_COLS 300
#define INPUT_SIZE 512
#define TIMER_CURSOR 1

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
static COLORREF currentColor = RGB(220, 220, 220);
static COLORREF backgroundColor = RGB(15, 17, 20);
static HWND mainWindow;
static HFONT terminalFont;

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

static void PutChar(char ch, COLORREF color) {
    if (ch == '\r') return;
    if (ch == '\n') {
        NewLine();
        return;
    }
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

static void ClearBuffer(void) {
    memset(screenBuffer, 0, sizeof(screenBuffer));
    usedRows = 1;
    cursorRow = cursorCol = scrollTop = 0;
    followOutput = 1;
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

static void ExecuteCommand(void) {
    input[inputLength] = '\0';

    if (strcmp(input, "help") == 0) {
        WriteLine("Comandos disponiveis:", RGB(100, 210, 130));
        WriteLine("  help  - mostra esta ajuda", currentColor);
        WriteLine("  clear - limpa a tela", currentColor);
        WriteLine("  echo  - repete um texto", currentColor);
        WriteLine("  color - muda a cor (ex: color blue)", currentColor);
        WriteLine("  exit  - fecha o terminal", currentColor);
    } else if (strcmp(input, "clear") == 0) {
        ClearBuffer();
    } else if (strncmp(input, "echo ", 5) == 0) {
        WriteLine(input + 5, currentColor);
    } else if (strcmp(input, "color blue") == 0) {
        currentColor = RGB(100, 160, 255);
    } else if (strcmp(input, "color green") == 0) {
        currentColor = RGB(100, 220, 130);
    } else if (strcmp(input, "color red") == 0) {
        currentColor = RGB(255, 100, 100);
    } else if (strcmp(input, "color white") == 0) {
        currentColor = RGB(230, 230, 230);
    } else if (strcmp(input, "exit") == 0) {
        PostMessageA(mainWindow, WM_CLOSE, 0, 0);
        return;
    } else if (inputLength > 0) {
        WriteText("Comando desconhecido: ", RGB(255, 110, 110));
        WriteLine(input, currentColor);
    }

    NewLine();
    PrintPrompt();
    InvalidateTerminal();
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
        ExecuteCommand();
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
    WriteLine("Native Terminal - prototipo Win32/GDI", RGB(100, 210, 140));
    WriteLine("Digite 'help' para ver os comandos.", RGB(150, 160, 175));
    PrintPrompt();

    MSG message;
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    if (terminalFont) DeleteObject(terminalFont);
    return 0;
}
