// The one translation unit that expands the DX11 overlay and the aim marker
// built on it.
//
// DXHRDC.exe carries strings for d3d9, d3d11 and dxgi and resolves its renderer
// at run time, and what it actually loads is the Direct3D 11 one: d3d11.dll,
// dxgi.dll and D3DCompiler_47.dll are all mapped into the running process, and
// no D3D9 entry point is ever called. So the marker attaches to the DXGI swap
// chain. If a future build or a fallback path turns out to render through D3D9,
// cameraunlock-core has AimMarkerDX9 ready to bind instead - but both backends
// installed at once is not the answer, and neither is guessing.
#include "aim_marker.h"

#include "logging.h"

#include <windows.h>

#include "MinHook.h"

#define CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION
#define CAMERAUNLOCK_AIM_MARKER_DX11_IMPLEMENTATION
#include "cameraunlock/rendering/aim_marker_dx11.h"

namespace DeusExHumanRevolutionHeadTracking {

namespace {

cameraunlock::rendering::AimMarkerDX11 s_marker;
bool s_loggedRenderer = false;

void MarkerLog(const char* msg) {
    Log::Line("%s", msg);
}

}  // namespace

bool EnsureAimMarker() {
    if (!s_loggedRenderer) {
        s_loggedRenderer = true;
        s_marker.SetLogger(&MarkerLog);
        if (GetModuleHandleA("d3d11.dll") == nullptr) {
            Log::Line("WARN: the aim marker attaches to Direct3D 11 and d3d11.dll is not "
                      "loaded in this process. The marker will not draw, and the marker "
                      "ADS mode behaves like the no-marker one.");
        }
    }
    return s_marker.Ensure();
}

void PublishAimMarker(bool visible, float ndcX, float ndcY) {
    s_marker.Publish(visible, ndcX, ndcY);
}

}  // namespace DeusExHumanRevolutionHeadTracking
