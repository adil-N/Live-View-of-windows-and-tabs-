#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <strsafe.h>
#include <wchar.h>
#include <wctype.h>
#include <stdlib.h>

#define APP_NAME L"DDF Window Capture"
#define DEFAULT_PATTERN L"*Self KYC Form*"
#define DEFAULT_WIDTH 480
#define DEFAULT_HEIGHT 320

#define IDC_WINDOW_LIST 1001
#define IDC_REFRESH 1002
#define IDC_OPEN 1003
#define IDC_CANCEL 1004
#define IDC_PICKER_LABEL 1005
#define TIMER_TARGET_CHECK 1

typedef struct WindowInfo {
    HWND handle;
    DWORD processId;
    WCHAR title[512];
    WCHAR processName[MAX_PATH];
} WindowInfo;

static HINSTANCE g_instance;
static HWND g_picker;
static HWND g_windowList;
static HWND g_viewer;
static HWND g_target;
static HTHUMBNAIL g_thumbnail;
static WindowInfo *g_windows;
static size_t g_windowCount;
static size_t g_windowCapacity;
static WCHAR g_targetTitle[512];
static WCHAR g_titlePattern[512] = DEFAULT_PATTERN;
static int g_previewWidth = DEFAULT_WIDTH;
static int g_previewHeight = DEFAULT_HEIGHT;
static BOOL g_forcePicker = FALSE;

static void LogMessage(const WCHAR *message)
{
    WCHAR tempPath[MAX_PATH];
    WCHAR logPath[MAX_PATH];
    SYSTEMTIME now;
    HANDLE file;
    WCHAR line[2048];
    char utf8[8192];
    int utf8Length;
    DWORD written;

    if (!GetTempPathW((DWORD)(sizeof(tempPath) / sizeof(tempPath[0])), tempPath)) {
        return;
    }
    if (FAILED(StringCchPrintfW(logPath, MAX_PATH, L"%sDDF-Window-Capture-error.log", tempPath))) {
        return;
    }

    GetLocalTime(&now);
    if (FAILED(StringCchPrintfW(
            line,
            sizeof(line) / sizeof(line[0]),
            L"%04u-%02u-%02uT%02u:%02u:%02u  %s\r\n",
            now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond,
            message))) {
        return;
    }

    utf8Length = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, (int)sizeof(utf8), NULL, NULL);
    if (utf8Length <= 1) {
        return;
    }

    file = CreateFileW(
        logPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    WriteFile(file, utf8, (DWORD)(utf8Length - 1), &written, NULL);
    CloseHandle(file);
}

static void ShowError(HWND owner, const WCHAR *message)
{
    LogMessage(message);
    MessageBoxW(owner, message, APP_NAME, MB_OK | MB_ICONERROR);
}

static BOOL WildcardMatchInsensitive(const WCHAR *pattern, const WCHAR *text)
{
    const WCHAR *star = NULL;
    const WCHAR *retry = NULL;

    while (*text) {
        if (*pattern == L'?' || towlower(*pattern) == towlower(*text)) {
            ++pattern;
            ++text;
        } else if (*pattern == L'*') {
            star = pattern++;
            retry = text;
        } else if (star) {
            pattern = star + 1;
            text = ++retry;
        } else {
            return FALSE;
        }
    }
    while (*pattern == L'*') {
        ++pattern;
    }
    return *pattern == L'\0';
}

static void ClearWindowList(void)
{
    if (g_windows) {
        HeapFree(GetProcessHeap(), 0, g_windows);
        g_windows = NULL;
    }
    g_windowCount = 0;
    g_windowCapacity = 0;
}

static BOOL EnsureWindowCapacity(void)
{
    WindowInfo *resized;
    size_t newCapacity;

    if (g_windowCount < g_windowCapacity) {
        return TRUE;
    }
    newCapacity = g_windowCapacity ? g_windowCapacity * 2 : 32;
    if (g_windows) {
        resized = (WindowInfo *)HeapReAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, g_windows, newCapacity * sizeof(WindowInfo));
    } else {
        resized = (WindowInfo *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, newCapacity * sizeof(WindowInfo));
    }
    if (!resized) {
        return FALSE;
    }
    g_windows = resized;
    g_windowCapacity = newCapacity;
    return TRUE;
}

static void GetProcessDisplayName(DWORD processId, WCHAR *buffer, size_t bufferCount)
{
    HANDLE process;
    WCHAR fullPath[MAX_PATH];
    DWORD pathLength = MAX_PATH;
    const WCHAR *baseName;

    buffer[0] = L'\0';
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process) {
        if (QueryFullProcessImageNameW(process, 0, fullPath, &pathLength)) {
            baseName = wcsrchr(fullPath, L'\\');
            baseName = baseName ? baseName + 1 : fullPath;
            StringCchCopyW(buffer, bufferCount, baseName);
        }
        CloseHandle(process);
    }
    if (!buffer[0]) {
        StringCchPrintfW(buffer, bufferCount, L"PID %lu", processId);
    }
}

static BOOL CALLBACK CollectWindow(HWND handle, LPARAM parameter)
{
    LONG_PTR extendedStyle;
    int titleLength;
    DWORD processId;
    WindowInfo *item;
    DWORD ownProcessId = GetCurrentProcessId();
    (void)parameter;

    if (!IsWindowVisible(handle) || GetAncestor(handle, GA_ROOT) != handle) {
        return TRUE;
    }
    extendedStyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    if ((extendedStyle & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }
    titleLength = GetWindowTextLengthW(handle);
    if (titleLength <= 0) {
        return TRUE;
    }
    GetWindowThreadProcessId(handle, &processId);
    if (processId == ownProcessId) {
        return TRUE;
    }
    if (!EnsureWindowCapacity()) {
        return FALSE;
    }

    item = &g_windows[g_windowCount];
    item->handle = handle;
    item->processId = processId;
    GetWindowTextW(handle, item->title, (int)(sizeof(item->title) / sizeof(item->title[0])));
    if (!item->title[0]) {
        return TRUE;
    }
    GetProcessDisplayName(processId, item->processName, sizeof(item->processName) / sizeof(item->processName[0]));
    ++g_windowCount;
    return TRUE;
}

static void EnumerateCandidateWindows(void)
{
    ClearWindowList();
    EnumWindows(CollectWindow, 0);
}

static HWND FindFirstMatchingWindow(void)
{
    size_t index;
    EnumerateCandidateWindows();
    for (index = 0; index < g_windowCount; ++index) {
        if (WildcardMatchInsensitive(g_titlePattern, g_windows[index].title)) {
            return g_windows[index].handle;
        }
    }
    return NULL;
}

static void PopulatePicker(void)
{
    size_t index;
    WCHAR row[1024];
    LRESULT itemIndex;

    if (!g_windowList) {
        return;
    }
    SendMessageW(g_windowList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_windowList, LB_RESETCONTENT, 0, 0);
    EnumerateCandidateWindows();
    for (index = 0; index < g_windowCount; ++index) {
        StringCchPrintfW(row, sizeof(row) / sizeof(row[0]), L"%s    [%s]", g_windows[index].title, g_windows[index].processName);
        itemIndex = SendMessageW(g_windowList, LB_ADDSTRING, 0, (LPARAM)row);
        if (itemIndex != LB_ERR && itemIndex != LB_ERRSPACE) {
            SendMessageW(g_windowList, LB_SETITEMDATA, (WPARAM)itemIndex, (LPARAM)index);
        }
    }
    if (g_windowCount > 0) {
        SendMessageW(g_windowList, LB_SETCURSEL, 0, 0);
    }
    SendMessageW(g_windowList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_windowList, NULL, TRUE);
}

static void UpdateThumbnailRectangle(void)
{
    RECT client;
    DWM_THUMBNAIL_PROPERTIES properties;

    if (!g_viewer || !g_thumbnail) {
        return;
    }
    GetClientRect(g_viewer, &client);
    ZeroMemory(&properties, sizeof(properties));
    properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
    properties.rcDestination = client;
    properties.opacity = 255;
    properties.fVisible = TRUE;
    properties.fSourceClientAreaOnly = FALSE;
    DwmUpdateThumbnailProperties(g_thumbnail, &properties);
}

static LRESULT CALLBACK ViewerWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    HRESULT result;
    WCHAR errorText[512];
    (void)wParam;
    (void)lParam;

    switch (message) {
    case WM_CREATE:
        result = DwmRegisterThumbnail(window, g_target, &g_thumbnail);
        if (FAILED(result)) {
            StringCchPrintfW(
                errorText,
                sizeof(errorText) / sizeof(errorText[0]),
                L"Windows could not create the live preview (HRESULT 0x%08lX). Confirm that Desktop Window Manager is running.",
                (unsigned long)result);
            ShowError(window, errorText);
            return -1;
        }
        SetTimer(window, TIMER_TARGET_CHECK, 1000, NULL);
        return 0;

    case WM_SIZE:
        UpdateThumbnailRectangle();
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_TARGET_CHECK && !IsWindow(g_target)) {
            MessageBoxW(window, L"The captured window has been closed.", APP_NAME, MB_OK | MB_ICONINFORMATION);
            DestroyWindow(window);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        KillTimer(window, TIMER_TARGET_CHECK);
        if (g_thumbnail) {
            DwmUnregisterThumbnail(g_thumbnail);
            g_thumbnail = NULL;
        }
        g_viewer = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static BOOL CreateViewer(HWND target)
{
    WCHAR caption[768];
    RECT requested;
    int outerWidth;
    int outerHeight;

    if (!IsWindow(target)) {
        ShowError(g_picker, L"The selected window is no longer available. Click Refresh and select it again.");
        return FALSE;
    }
    g_target = target;
    GetWindowTextW(target, g_targetTitle, (int)(sizeof(g_targetTitle) / sizeof(g_targetTitle[0])));
    StringCchPrintfW(caption, sizeof(caption) / sizeof(caption[0]), L"Live: %s", g_targetTitle);

    requested.left = 0;
    requested.top = 0;
    requested.right = g_previewWidth;
    requested.bottom = g_previewHeight;
    AdjustWindowRectEx(&requested, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_TOPMOST);
    outerWidth = requested.right - requested.left;
    outerHeight = requested.bottom - requested.top;

    g_viewer = CreateWindowExW(
        WS_EX_TOPMOST,
        L"DDFWindowCaptureViewer",
        caption,
        WS_OVERLAPPEDWINDOW,
        60,
        60,
        outerWidth,
        outerHeight,
        NULL,
        NULL,
        g_instance,
        NULL);
    if (!g_viewer) {
        ShowError(g_picker, L"The preview window could not be created.");
        return FALSE;
    }
    ShowWindow(g_viewer, SW_SHOWNORMAL);
    UpdateWindow(g_viewer);
    UpdateThumbnailRectangle();
    return TRUE;
}

static BOOL OpenPickerSelection(HWND picker)
{
    LRESULT selected;
    LRESULT dataIndex;
    HWND target;

    selected = SendMessageW(g_windowList, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        MessageBoxW(picker, L"Select a window first.", APP_NAME, MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }
    dataIndex = SendMessageW(g_windowList, LB_GETITEMDATA, (WPARAM)selected, 0);
    if (dataIndex == LB_ERR || (size_t)dataIndex >= g_windowCount) {
        ShowError(picker, L"That list entry is no longer valid. Click Refresh and try again.");
        return FALSE;
    }
    target = g_windows[(size_t)dataIndex].handle;
    if (!IsWindow(target)) {
        ShowError(picker, L"The selected window has already closed. Click Refresh and try again.");
        return FALSE;
    }
    if (!CreateViewer(target)) {
        return FALSE;
    }
    DestroyWindow(picker);
    return TRUE;
}

static void LayoutPicker(HWND window)
{
    RECT client;
    int width;
    int height;
    int margin = 12;
    int buttonWidth = 92;
    int buttonHeight = 28;
    int buttonY;
    int listTop = 38;

    GetClientRect(window, &client);
    width = client.right - client.left;
    height = client.bottom - client.top;
    buttonY = height - margin - buttonHeight;

    MoveWindow(GetDlgItem(window, IDC_PICKER_LABEL), margin, 11, width - margin * 2, 20, TRUE);
    MoveWindow(g_windowList, margin, listTop, width - margin * 2, buttonY - listTop - margin, TRUE);
    MoveWindow(GetDlgItem(window, IDC_REFRESH), margin, buttonY, buttonWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(window, IDC_OPEN), width - margin - (buttonWidth * 2) - 8, buttonY, buttonWidth, buttonHeight, TRUE);
    MoveWindow(GetDlgItem(window, IDC_CANCEL), width - margin - buttonWidth, buttonY, buttonWidth, buttonHeight, TRUE);
}

static LRESULT CALLBACK PickerWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    HFONT font;

    switch (message) {
    case WM_CREATE:
        font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        CreateWindowExW(0, L"STATIC", L"Select an open window to show in the live preview:",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_PICKER_LABEL, g_instance, NULL);
        g_windowList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_WINDOW_LIST, g_instance, NULL);
        CreateWindowExW(0, L"BUTTON", L"Refresh",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_REFRESH, g_instance, NULL);
        CreateWindowExW(0, L"BUTTON", L"Open preview",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_OPEN, g_instance, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_CANCEL, g_instance, NULL);

        SendMessageW(GetDlgItem(window, IDC_PICKER_LABEL), WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_windowList, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(GetDlgItem(window, IDC_REFRESH), WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(GetDlgItem(window, IDC_OPEN), WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(GetDlgItem(window, IDC_CANCEL), WM_SETFONT, (WPARAM)font, TRUE);
        PopulatePicker();
        return 0;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lParam)->ptMinTrackSize.x = 500;
        ((MINMAXINFO *)lParam)->ptMinTrackSize.y = 300;
        return 0;

    case WM_SIZE:
        LayoutPicker(window);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_REFRESH) {
            PopulatePicker();
            return 0;
        }
        if (LOWORD(wParam) == IDC_OPEN ||
            (LOWORD(wParam) == IDC_WINDOW_LIST && HIWORD(wParam) == LBN_DBLCLK)) {
            OpenPickerSelection(window);
            return 0;
        }
        if (LOWORD(wParam) == IDC_CANCEL) {
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        g_picker = NULL;
        g_windowList = NULL;
        if (!g_viewer) {
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static BOOL CreatePicker(void)
{
    g_picker = CreateWindowExW(
        0,
        L"DDFWindowCapturePicker",
        L"Choose a window - DDF Window Capture",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        760,
        480,
        NULL,
        NULL,
        g_instance,
        NULL);
    if (!g_picker) {
        ShowError(NULL, L"The window picker could not be created.");
        return FALSE;
    }
    ShowWindow(g_picker, SW_SHOWNORMAL);
    UpdateWindow(g_picker);
    SetForegroundWindow(g_picker);
    return TRUE;
}

static int ParsePositiveInteger(const WCHAR *text, int fallback, int minimum, int maximum)
{
    WCHAR *end = NULL;
    long value = wcstol(text, &end, 10);
    if (!text[0] || (end && *end) || value < minimum || value > maximum) {
        return fallback;
    }
    return (int)value;
}

static BOOL ParseCommandLine(void)
{
    int argumentCount = 0;
    int index;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments) {
        return TRUE;
    }

    for (index = 1; index < argumentCount; ++index) {
        if ((!lstrcmpiW(arguments[index], L"--title") || !lstrcmpiW(arguments[index], L"-t")) && index + 1 < argumentCount) {
            StringCchCopyW(g_titlePattern, sizeof(g_titlePattern) / sizeof(g_titlePattern[0]), arguments[++index]);
        } else if (!lstrcmpiW(arguments[index], L"--width") && index + 1 < argumentCount) {
            g_previewWidth = ParsePositiveInteger(arguments[++index], DEFAULT_WIDTH, 160, 7680);
        } else if (!lstrcmpiW(arguments[index], L"--height") && index + 1 < argumentCount) {
            g_previewHeight = ParsePositiveInteger(arguments[++index], DEFAULT_HEIGHT, 120, 4320);
        } else if (!lstrcmpiW(arguments[index], L"--picker") || !lstrcmpiW(arguments[index], L"--pick")) {
            g_forcePicker = TRUE;
        } else if (!lstrcmpiW(arguments[index], L"--help") || !lstrcmpiW(arguments[index], L"-h") || !lstrcmpiW(arguments[index], L"/?")) {
            MessageBoxW(
                NULL,
                L"DDF Window Capture\r\n\r\n"
                L"Double-click the executable to capture the first window matching *Self KYC Form*.\r\n"
                L"If no match exists, a graphical window picker opens.\r\n\r\n"
                L"Optional command-line arguments:\r\n"
                L"  --picker\r\n"
                L"  --title \"*window title*\"\r\n"
                L"  --width 480 --height 320",
                APP_NAME,
                MB_OK | MB_ICONINFORMATION);
            LocalFree(arguments);
            return FALSE;
        }
    }
    LocalFree(arguments);
    return TRUE;
}

static BOOL RegisterWindowClasses(void)
{
    WNDCLASSEXW windowClass;

    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = g_instance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    windowClass.hIconSm = LoadIconW(NULL, IDI_APPLICATION);
    windowClass.lpfnWndProc = ViewerWindowProc;
    windowClass.lpszClassName = L"DDFWindowCaptureViewer";
    windowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (!RegisterClassExW(&windowClass)) {
        return FALSE;
    }

    windowClass.lpfnWndProc = PickerWindowProc;
    windowClass.lpszClassName = L"DDFWindowCapturePicker";
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&windowClass)) {
        return FALSE;
    }
    return TRUE;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand)
{
    INITCOMMONCONTROLSEX controls;
    MSG message;
    HWND match;
    (void)previousInstance;
    (void)commandLine;
    (void)showCommand;

    g_instance = instance;
    SetProcessDPIAware();
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    if (!ParseCommandLine()) {
        return 0;
    }
    if (!RegisterWindowClasses()) {
        ShowError(NULL, L"The application could not initialize its window classes.");
        return 1;
    }

    match = g_forcePicker ? NULL : FindFirstMatchingWindow();
    if (match) {
        if (!CreateViewer(match)) {
            ClearWindowList();
            return 1;
        }
    } else if (!CreatePicker()) {
        ClearWindowList();
        return 1;
    }

    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (!g_picker || !IsDialogMessageW(g_picker, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    ClearWindowList();
    return (int)message.wParam;
}
