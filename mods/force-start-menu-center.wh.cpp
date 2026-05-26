// ==WindhawkMod==
// @id              force-start-menu-center
// @name            Force Start Menu Center
// @description     Forces the Start menu to always open in the center of the screen, regardless of the Start button position
// @version         1.0
// @author          exabytec
// @github          https://github.com/exabytec
// @include         StartMenuExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject
// @license         GPL-3.0
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Force Start Menu Center

Forces the Start menu to always open in the center of the screen, regardless
of where the Start button is positioned on the taskbar.

Useful when paired with mods that move the Start button (e.g. "Start button
always on the left"), where the Start menu would otherwise follow the button
position instead of staying centered.

Only Windows 11 is supported.

## Credits
Based on code from the
[taskbar-start-button-position](https://windhawk.net/mods/taskbar-start-button-position)
mod by [m417z](https://github.com/m417z).
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>
#include <atomic>
#include <optional>
#include <roapi.h>
#include <winstring.h>

#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

using namespace winrt::Windows::UI::Xaml;

std::atomic<bool> g_unloading;

FrameworkElement FindChildByName(FrameworkElement element, PCWSTR name) {
    int count = Media::VisualTreeHelper::GetChildrenCount(element);
    for (int i = 0; i < count; i++) {
        auto child = Media::VisualTreeHelper::GetChild(element, i)
                         .try_as<FrameworkElement>();
        if (child && child.Name() == name) return child;
    }
    return nullptr;
}

FrameworkElement FindChildByClassName(FrameworkElement element, PCWSTR className) {
    int count = Media::VisualTreeHelper::GetChildrenCount(element);
    for (int i = 0; i < count; i++) {
        auto child = Media::VisualTreeHelper::GetChild(element, i)
                         .try_as<FrameworkElement>();
        if (child && winrt::get_class_name(child) == className) return child;
    }
    return nullptr;
}

namespace StartMenuUI {

bool g_applyStylePending;
bool g_inApplyStyle;
std::optional<double> g_previousCanvasLeft;
std::optional<HorizontalAlignment> g_previousHorizontalAlignment;
winrt::event_token g_layoutUpdatedToken;
winrt::event_token g_visibilityChangedToken;

HWND GetCoreWnd() {
    HWND hWnd = nullptr;
    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        DWORD dwProcessId = 0;
        WCHAR szClassName[32];
        if (GetWindowThreadProcessId(hWnd, &dwProcessId) &&
            dwProcessId == GetCurrentProcessId() &&
            GetClassName(hWnd, szClassName, ARRAYSIZE(szClassName)) &&
            _wcsicmp(szClassName, L"Windows.UI.Core.CoreWindow") == 0) {
            *reinterpret_cast<HWND*>(lParam) = hWnd;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&hWnd);
    return hWnd;
}

void ApplyStyle();

void ApplyStyleClassicStartMenu(FrameworkElement content) {
    FrameworkElement startSizingFrame =
        FindChildByClassName(content, L"StartDocked.StartSizingFrame");
    if (!startSizingFrame) {
        Wh_Log(L"Failed to find StartDocked.StartSizingFrame");
        return;
    }

    if (g_unloading) {
        if (g_previousCanvasLeft.has_value()) {
            Controls::Canvas::SetLeft(startSizingFrame, g_previousCanvasLeft.value());
        }
        return;
    }

    if (!g_previousCanvasLeft.has_value()) {
        double canvasLeft = Controls::Canvas::GetLeft(startSizingFrame);
        if (canvasLeft) g_previousCanvasLeft = canvasLeft;
    }

    HWND coreWnd = GetCoreWnd();
    HMONITOR monitor = MonitorFromWindow(coreWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{ .cbSize = sizeof(MONITORINFO) };
    GetMonitorInfo(monitor, &monitorInfo);

    int screenWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    double menuWidth = startSizingFrame.ActualWidth();
    double newLeft = (screenWidth - menuWidth) / 2.0;

    Wh_Log(L"Setting Canvas.Left to center: %f", newLeft);
    Controls::Canvas::SetLeft(startSizingFrame, newLeft);
}

void ApplyStyleRedesignedStartMenu(FrameworkElement content) {
    FrameworkElement frameRoot = FindChildByName(content, L"FrameRoot");
    if (!frameRoot) {
        Wh_Log(L"Failed to find FrameRoot");
        return;
    }

    if (g_unloading) {
        frameRoot.HorizontalAlignment(
            g_previousHorizontalAlignment.value_or(HorizontalAlignment::Center));
        return;
    }

    if (!g_previousHorizontalAlignment)
        g_previousHorizontalAlignment = frameRoot.HorizontalAlignment();

    frameRoot.HorizontalAlignment(HorizontalAlignment::Center);
}

void ApplyStyle() {
    g_inApplyStyle = true;

    auto window = Window::Current();
    if (!window) { g_inApplyStyle = false; return; }

    FrameworkElement content = window.Content().as<FrameworkElement>();
    winrt::hstring className = winrt::get_class_name(content);
    Wh_Log(L"Start menu class: %s", className.c_str());

    if (className == L"Windows.UI.Xaml.Controls.Canvas") {
        ApplyStyleClassicStartMenu(content);
    } else if (className == L"StartMenu.StartBlendedFlexFrame") {
        ApplyStyleRedesignedStartMenu(content);
    } else {
        Wh_Log(L"Unsupported Start menu class");
    }

    g_inApplyStyle = false;
}

void Init() {
    if (g_layoutUpdatedToken) return;

    auto window = Window::Current();
    if (!window) return;

    if (!g_visibilityChangedToken) {
        g_visibilityChangedToken = window.VisibilityChanged(
            [](winrt::Windows::Foundation::IInspectable const&,
               winrt::Windows::UI::Core::VisibilityChangedEventArgs const& args) {
                if (args.Visible()) g_applyStylePending = true;
            });
    }

    auto content = window.Content().as<FrameworkElement>();
    if (!content) return;

    g_layoutUpdatedToken = content.LayoutUpdated(
        [](winrt::Windows::Foundation::IInspectable const&,
           winrt::Windows::Foundation::IInspectable const&) {
            if (g_applyStylePending) {
                g_applyStylePending = false;
                ApplyStyle();
            }
        });

    ApplyStyle();
}

void Uninit() {
    if (!g_layoutUpdatedToken) return;

    auto window = Window::Current();
    if (!window) return;

    if (g_visibilityChangedToken) {
        window.VisibilityChanged(g_visibilityChangedToken);
        g_visibilityChangedToken = {};
    }

    auto content = window.Content().as<FrameworkElement>();
    if (content) {
        content.LayoutUpdated(g_layoutUpdatedToken);
        g_layoutUpdatedToken = {};
    }

    ApplyStyle();
}

using RoGetActivationFactory_t = decltype(&RoGetActivationFactory);
RoGetActivationFactory_t RoGetActivationFactory_Original;
HRESULT WINAPI RoGetActivationFactory_Hook(HSTRING activatableClassId,
                                           REFIID iid, void** factory) {
    thread_local static bool isInHook;
    if (isInHook)
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);

    isInHook = true;

    if (wcscmp(WindowsGetStringRawBuffer(activatableClassId, nullptr),
               L"Windows.UI.Xaml.Hosting.XamlIsland") == 0) {
        try { Init(); } catch (...) {}
    }

    HRESULT ret = RoGetActivationFactory_Original(activatableClassId, iid, factory);
    isInHook = false;
    return ret;
}

}  // namespace StartMenuUI

using RunFromWindowThreadProc_t = void(WINAPI*)(void* parameter);

bool RunFromWindowThread(HWND hWnd, RunFromWindowThreadProc_t proc, void* param) {
    static const UINT msg =
        RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);

    struct PARAM { RunFromWindowThreadProc_t proc; void* param; };

    DWORD tid = GetWindowThreadProcessId(hWnd, nullptr);
    if (!tid) return false;
    if (tid == GetCurrentThreadId()) { proc(param); return true; }

    HHOOK hook = SetWindowsHookEx(WH_CALLWNDPROC,
        [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (nCode == HC_ACTION) {
                auto* cwp = (CWPSTRUCT*)lParam;
                if (cwp->message == RegisterWindowMessage(
                        L"Windhawk_RunFromWindowThread_" WH_MOD_ID)) {
                    auto* p = (PARAM*)cwp->lParam;
                    p->proc(p->param);
                }
            }
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }, nullptr, tid);
    if (!hook) return false;

    PARAM p{ proc, param };
    SendMessage(hWnd, msg, 0, (LPARAM)&p);
    UnhookWindowsHookEx(hook);
    return true;
}

BOOL Wh_ModInit() {
    Wh_Log(L">");

    HMODULE winrtModule = GetModuleHandle(L"api-ms-win-core-winrt-l1-1-0.dll");
    auto pRoGetActivationFactory =
        (decltype(&RoGetActivationFactory))GetProcAddress(
            winrtModule, "RoGetActivationFactory");

    WindhawkUtils::SetFunctionHook(
        pRoGetActivationFactory,
        StartMenuUI::RoGetActivationFactory_Hook,
        &StartMenuUI::RoGetActivationFactory_Original);

    return TRUE;
}

void Wh_ModAfterInit() {
    Wh_Log(L">");
    HWND hCoreWnd = StartMenuUI::GetCoreWnd();
    if (hCoreWnd) {
        RunFromWindowThread(hCoreWnd, [](PVOID) { StartMenuUI::Init(); }, nullptr);
    }
}

void Wh_ModBeforeUninit() {
    Wh_Log(L">");
    g_unloading = true;
    HWND hCoreWnd = StartMenuUI::GetCoreWnd();
    if (hCoreWnd) {
        RunFromWindowThread(hCoreWnd, [](PVOID) { StartMenuUI::Uninit(); }, nullptr);
    }
}

void Wh_ModUninit() {
    Wh_Log(L">");
}
