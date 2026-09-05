#include "ads.h"
#include "build_profile.h"
#include "camera_hook.h"
#include "config.h"
#include "hotkeys.h"
#include "logging.h"
#include "path_utils.h"
#include "reticle_hook.h"
#include "tracking_runtime.h"

#include "cameraunlock/diagnostics/crash_handler.h"

#include "MinHook.h"

#include <windows.h>
#include <process.h>

namespace {

constexpr const char* kGameExe = "DXHRDC.exe";
constexpr int kInitMaxWaitMs = 30000;
constexpr int kInitPollMs    = 100;

HANDLE g_initThreadHandle = nullptr;

DeusExHumanRevolutionHeadTracking::TrackingRuntime g_tracking;
DeusExHumanRevolutionHeadTracking::Hotkeys         g_hotkeys;
DeusExHumanRevolutionHeadTracking::CameraHook      g_cameraHook;
DeusExHumanRevolutionHeadTracking::ReticleHook     g_reticleHook;
DeusExHumanRevolutionHeadTracking::AdsHook         g_adsHook;

unsigned __stdcall InitThread(void*) {
    using namespace DeusExHumanRevolutionHeadTracking;

    int waited = 0;
    while (!GetModuleHandleA(kGameExe)) {
        Sleep(kInitPollMs);
        waited += kInitPollMs;
        if (waited >= kInitMaxWaitMs) {
            return 1;
        }
    }

    // Open() truncates, so every launch starts from an empty log, and keeps one
    // generation back as HeadTracking.prev.log - the crash handler writes its
    // report into the live log and the player's next action after a crash is to
    // relaunch, which would otherwise destroy it.
    Log::Open(GetModulePathW("HeadTracking.log"));
    cameraunlock::diagnostics::InstallCrashHandler();

    Log::Line("DeusExHumanRevolutionHeadTracking v0.0.0 attached to %s", kGameExe);

    // Fingerprint the running EXE first. An unrecognised build leaves the mod
    // dormant - nothing hooked, no process modification at all - rather than
    // detouring stale RVAs, so nothing below this may hook anything either.
    const BuildProfile* profile = MatchRunningBuild();
    if (profile == nullptr) {
        LogUnmatchedBuildDiagnostic();
    }

    // MinHook is process-wide and four hooks in this mod share it - the camera,
    // the reticle, the sights, and the aim marker's overlay, which brings itself
    // up from a frame long after this returns. So it is initialised here and
    // torn down at detach, after all four, rather than owned by whichever hook
    // happened to install first.
    if (profile != nullptr && MH_Initialize() != MH_OK) {
        Log::Line("ERROR: MH_Initialize failed");
        return 1;
    }

    Config cfg;
    std::string iniPath = GetModulePath("DeusExHumanRevolutionHeadTracking.ini");
    if (!cfg.LoadOrCreate(iniPath.c_str())) {
        Log::Line("ERROR: Config load failed");
        return 1;
    }
    Log::Line("Config: port=%u enabled=%d smoothing=(local %.2f, remote %.2f) sens=(%.2f,%.2f,%.2f)",
              cfg.udp_port, cfg.enabled_on_startup ? 1 : 0,
              cfg.local_smoothing, cfg.remote_smoothing,
              cfg.sens_yaw, cfg.sens_pitch, cfg.sens_roll);

    if (!g_tracking.Start(cfg, iniPath)) {
        Log::Line("ERROR: Tracking runtime start failed");
        return 1;
    }

    if (!g_hotkeys.Start(cfg,
                        [] { g_tracking.ToggleEnabled(); },
                        [] { g_tracking.CycleTrackingMode(); },
                        [] { g_tracking.ToggleYawMode(); },
                        [] { g_tracking.CycleAdsMode(); })) {
        Log::Line("ERROR: Hotkeys start failed");
        g_tracking.Stop();
        return 1;
    }

    // The UDP receiver and the hotkeys stay up on an unrecognised build; the
    // camera does not.
    if (profile == nullptr) {
        Log::Line("DeusExHumanRevolutionHeadTracking ready (camera hook dormant)");
        return 0;
    }

    if (!g_cameraHook.Install(*profile, cfg, &g_tracking)) {
        Log::Line("ERROR: Camera hook install failed");
        g_hotkeys.Stop();
        g_tracking.Stop();
        return 1;
    }

    // Whether the sights are up. Everything downstream polls it, so it goes up
    // before the reticle hook, which reads it to decide whether to draw the ADS
    // marker.
    g_adsHook.Install(*profile);

    // The reticle only needs moving once aim is decoupled, so it follows the
    // camera hook and a failure there leaves the camera working.
    g_reticleHook.Install(*profile, cfg, &g_tracking);

    Log::Line("DeusExHumanRevolutionHeadTracking ready");
    return 0;
}

}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            g_initThreadHandle = reinterpret_cast<HANDLE>(
                _beginthreadex(nullptr, 0, InitThread, nullptr, 0, nullptr));
            break;

        case DLL_PROCESS_DETACH:
            // reserved != nullptr means the process is terminating: every other
            // thread has already been forcibly killed, possibly mid-lock. Joining
            // them here (under the loader lock) is the classic DllMain deadlock
            // and buys nothing - the OS reclaims everything.
            if (reserved != nullptr) {
                break;
            }
            if (g_initThreadHandle) {
                WaitForSingleObject(g_initThreadHandle, 2000);
                CloseHandle(g_initThreadHandle);
                g_initThreadHandle = nullptr;
            }
            g_reticleHook.Uninstall();
            g_adsHook.Uninstall();
            g_cameraHook.Uninstall();
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            g_hotkeys.Stop();
            g_tracking.Stop();
            DeusExHumanRevolutionHeadTracking::Log::Close();
            break;
    }
    return TRUE;
}
