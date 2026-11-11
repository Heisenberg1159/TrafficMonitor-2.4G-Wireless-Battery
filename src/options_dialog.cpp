#include "options_dialog.h"

#include <commctrl.h>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")

namespace {

HMODULE SelfModule() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&RunOptionsDialog), &self);
    return self;
}

INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* v = reinterpret_cast<OptionsValues*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, lParam);
            HWND combo = GetDlgItem(hwnd, IDC_STYLE);
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"图标（设备图标 + 电池 + 百分比）"));
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"纯文字（标签 + 百分比）"));
            SendMessageW(combo, CB_SETCURSEL, v->iconStyle ? 0 : 1, 0);
            SendDlgItemMessageW(hwnd, IDC_POLL, EM_LIMITTEXT, 4, 0);
            SendDlgItemMessageW(hwnd, IDC_POLL_SPIN, UDM_SETRANGE32, kMinPollSeconds, kMaxPollSeconds);
            SendDlgItemMessageW(hwnd, IDC_POLL_SPIN, UDM_SETPOS32, 0, v->pollSeconds);
            SetDlgItemInt(hwnd, IDC_POLL, static_cast<UINT>(v->pollSeconds), FALSE);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    auto* v = reinterpret_cast<OptionsValues*>(GetWindowLongPtrW(hwnd, DWLP_USER));
                    BOOL ok = FALSE;
                    UINT poll = GetDlgItemInt(hwnd, IDC_POLL, &ok, FALSE);
                    if (!ok || poll < kMinPollSeconds || poll > kMaxPollSeconds) {
                        MessageBoxW(hwnd, L"轮询间隔请输入 10 ~ 3600 之间的整数（秒）。", L"无线设备电量",
                                    MB_ICONWARNING | MB_OK);
                        HWND edit = GetDlgItem(hwnd, IDC_POLL);
                        SetFocus(edit);
                        SendMessageW(edit, EM_SETSEL, 0, -1);
                        return TRUE;
                    }
                    v->pollSeconds = static_cast<int>(poll);
                    v->iconStyle = SendDlgItemMessageW(hwnd, IDC_STYLE, CB_GETCURSEL, 0, 0) != 1;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

}  // namespace

bool RunOptionsDialog(HWND parent, OptionsValues& values) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    OptionsValues edited = values;
    INT_PTR r = DialogBoxParamW(SelfModule(), MAKEINTRESOURCEW(IDD_OPTIONS), parent, DialogProc,
                                reinterpret_cast<LPARAM>(&edited));
    if (r != IDOK) return false;
    values = edited;
    return true;
}
