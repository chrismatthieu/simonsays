// Simon Says: Only the enrolled player can make the stick man dance.
// Uses RealSense ID for face enrollment/authentication and pose estimation.

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/FaceRect.h"
#include "RealSenseID/FacePose.h"
#include "RealSenseID/DiscoverDevices.h"
#include "RealSenseID/Version.h"
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define ISATTY _isatty(_fileno(stdin))
#else
#include <unistd.h>
#define ISATTY isatty(STDIN_FILENO)
#endif

#ifndef SIMONSAYS_NO_SDL
#include <SDL.h>
#endif

namespace {

const char* DEFAULT_USER_ID = "player1";
constexpr int POSE_WINDOW_W = 640;
constexpr int POSE_WINDOW_H = 480;
// Camera frame size used by RealSense ID for pose (FHD)
constexpr double CAM_WIDTH = 1920.0;
constexpr double CAM_HEIGHT = 1080.0;

// COCO keypoints: 0=Nose, 1=LeftEye, 2=RightEye, 3=LeftEar, 4=RightEar,
// 5=LeftShoulder, 6=RightShoulder, 7=LeftElbow, 8=RightElbow, 9=LeftWrist, 10=RightWrist,
// 11=LeftHip, 12=RightHip, 13=LeftKnee, 14=RightKnee, 15=LeftAnkle, 16=RightAnkle
const std::vector<std::pair<int, int>> POSE_CONNECTIONS = {
    {15, 13}, {13, 11}, {16, 14}, {14, 12}, {11, 12}, {5, 11}, {6, 12},
    {5, 6},   {5, 7},   {7, 9},   {6, 8},   {8, 10},  {0, 1},  {0, 2}, {1, 3}, {2, 4}
};

// Auto-detect RealSense ID (prefer F460/F46x). RSID_PORT overrides.
// When type is Unknown (e.g. "Cannot detect device type"), assume F460 (F46x).
bool discover_rsid_device(std::string& out_port, RealSenseID::DeviceType& out_type) {
    const char* env = std::getenv("RSID_PORT");
    std::vector<RealSenseID::DeviceInfo> devices = RealSenseID::DiscoverDevices();
    if (devices.empty()) {
        if (env) {
            out_port = env;
            out_type = RealSenseID::DiscoverDeviceType(out_port.c_str());
            if (out_type == RealSenseID::DeviceType::Unknown)
                out_type = RealSenseID::DeviceType::F46x;  // assume F460
            return true;
        }
#ifdef _WIN32
        out_port = "COM4";
        out_type = RealSenseID::DeviceType::F46x;
        return true;
#else
        out_port = "/dev/ttyACM0";
        out_type = RealSenseID::DeviceType::F46x;
        return true;
#endif
    }
    if (env) {
        for (const auto& d : devices) {
            if (std::string(d.serialPort) == env) {
                out_port = d.serialPort;
                out_type = d.deviceType;
                if (out_type == RealSenseID::DeviceType::Unknown)
                    out_type = RealSenseID::DeviceType::F46x;
                return true;
            }
        }
        out_port = env;
        out_type = RealSenseID::DiscoverDeviceType(out_port.c_str());
        if (out_type == RealSenseID::DeviceType::Unknown)
            out_type = RealSenseID::DeviceType::F46x;
        return true;
    }
    // Prefer F460 (F46x), then F45x, then any
    const auto* chosen = &devices[0];
    for (const auto& d : devices) {
        if (d.deviceType == RealSenseID::DeviceType::F46x) {
            chosen = &d;
            break;
        }
        if (chosen->deviceType != RealSenseID::DeviceType::F46x && d.deviceType == RealSenseID::DeviceType::F45x)
            chosen = &d;
    }
    out_port = chosen->serialPort;
    out_type = chosen->deviceType;
    if (out_type == RealSenseID::DeviceType::Unknown)
        out_type = RealSenseID::DeviceType::F46x;  // assume F460 when SDK can't detect
    return true;
}

RealSenseID::SerialConfig get_serial_config(const char* port) {
    RealSenseID::SerialConfig c;
    c.port = port;
    return c;
}

// Latest pose from device (thread-safe)
std::mutex g_pose_mutex;
std::vector<RealSenseID::PersonPose> g_latest_poses;
std::atomic<bool> g_authenticated{false};
std::atomic<bool> g_pose_loop_running{false};
std::atomic<bool> g_quit{false};

// So Ctrl+C handler can call Cancel() on the SDK
static RealSenseID::FaceAuthenticator* g_authenticator_for_ctrl_c = nullptr;

#ifdef _WIN32
BOOL WINAPI ctrl_c_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        g_quit = true;
        if (g_authenticator_for_ctrl_c)
            g_authenticator_for_ctrl_c->Cancel();
        return TRUE;
    }
    return FALSE;
}
#else
void ctrl_c_handler(int) {
    g_quit = true;
    if (g_authenticator_for_ctrl_c)
        g_authenticator_for_ctrl_c->Cancel();
}
#endif

void update_poses(const std::vector<RealSenseID::PersonPose>& poses) {
    std::lock_guard<std::mutex> lock(g_pose_mutex);
    g_latest_poses = poses;
}

void get_poses_copy(std::vector<RealSenseID::PersonPose>& out) {
    std::lock_guard<std::mutex> lock(g_pose_mutex);
    out = g_latest_poses;
}

// ---- Enrollment ----
class EnrollCallback : public RealSenseID::EnrollmentCallback {
public:
    void OnResult(RealSenseID::EnrollStatus status) override {
        std::cout << "Enroll result: " << static_cast<int>(status) << std::endl;
    }
    void OnProgress(RealSenseID::FacePose pose) override {
        std::cout << "Pose: " << RealSenseID::Description(pose) << std::endl;
    }
    void OnHint(RealSenseID::EnrollStatus hint, float) override {
        std::cout << "Hint: " << static_cast<int>(hint) << std::endl;
    }
};

// ---- Auth (single shot) ----
class AuthCallback : public RealSenseID::AuthenticationCallback {
public:
    std::atomic<RealSenseID::AuthenticateStatus> result{RealSenseID::AuthenticateStatus::CameraStarted};
    std::string authenticated_user_id;

    void OnResult(RealSenseID::AuthenticateStatus status, const char* userId, short) override {
        result = status;
        if (userId) authenticated_user_id = userId;
    }
    void OnHint(RealSenseID::AuthenticateStatus hint, float) override {
        (void)hint;
    }
};

// ---- Pose loop callback (only used when AlgoFlow is PoseEstimationOnly) ----
class PoseLoopCallback : public RealSenseID::AuthenticationCallback {
public:
    void OnResult(RealSenseID::AuthenticateStatus, const char*, short) override {}
    void OnHint(RealSenseID::AuthenticateStatus, float) override {}
    void OnPoseDetected(const std::vector<RealSenseID::PersonPose>& poses, unsigned int) override {
        update_poses(poses);
    }
};

#ifndef SIMONSAYS_NO_SDL
bool init_sdl(SDL_Window*& window, SDL_Renderer*& renderer) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init: " << SDL_GetError() << std::endl;
        return false;
    }
    window = SDL_CreateWindow("Simon Says - Stick Man",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              POSE_WINDOW_W, POSE_WINDOW_H, 0);
    if (!window) {
        std::cerr << "SDL_CreateWindow: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return false;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }
    return true;
}

void draw_stick_man(SDL_Renderer* renderer, const std::vector<RealSenseID::PersonPose>& poses) {
    if (poses.empty()) return;
    const auto& p = poses[0];
    double scaleX = POSE_WINDOW_W / CAM_WIDTH;
    double scaleY = POSE_WINDOW_H / CAM_HEIGHT;

    auto draw_line = [&](int i, int j) {
        if (i >= NUM_POSE_LANDMARKS || j >= NUM_POSE_LANDMARKS) return;
        uint32_t x0 = p.lm_x[i], y0 = p.lm_y[i], x1 = p.lm_x[j], y1 = p.lm_y[j];
        if (x0 == 0 && y0 == 0) return;
        if (x1 == 0 && y1 == 0) return;
        SDL_RenderDrawLine(renderer,
                           static_cast<int>(x0 * scaleX), static_cast<int>(y0 * scaleY),
                           static_cast<int>(x1 * scaleX), static_cast<int>(y1 * scaleY));
    };

    SDL_SetRenderDrawColor(renderer, 0, 200, 100, 255);
    for (const auto& conn : POSE_CONNECTIONS)
        draw_line(conn.first, conn.second);

    SDL_SetRenderDrawColor(renderer, 255, 220, 0, 255);
    for (int i = 0; i < NUM_POSE_LANDMARKS; i++) {
        if (p.lm_x[i] == 0 && p.lm_y[i] == 0) continue;
        int cx = static_cast<int>(p.lm_x[i] * scaleX);
        int cy = static_cast<int>(p.lm_y[i] * scaleY);
        SDL_Rect r = { cx - 4, cy - 4, 8, 8 };
        SDL_RenderFillRect(renderer, &r);
    }
}
#endif

#ifdef SIMONSAYS_NO_SDL
#ifdef _WIN32
void draw_stick_man_gdi(HDC hdc, const std::vector<RealSenseID::PersonPose>& poses) {
    if (poses.empty()) return;
    const auto& p = poses[0];
    double scaleX = POSE_WINDOW_W / CAM_WIDTH;
    double scaleY = POSE_WINDOW_H / CAM_HEIGHT;

    auto draw_line = [&](int i, int j) {
        if (i >= NUM_POSE_LANDMARKS || j >= NUM_POSE_LANDMARKS) return;
        uint32_t x0 = p.lm_x[i], y0 = p.lm_y[i], x1 = p.lm_x[j], y1 = p.lm_y[j];
        if (x0 == 0 && y0 == 0) return;
        if (x1 == 0 && y1 == 0) return;
        MoveToEx(hdc, static_cast<int>(x0 * scaleX), static_cast<int>(y0 * scaleY), nullptr);
        LineTo(hdc, static_cast<int>(x1 * scaleX), static_cast<int>(y1 * scaleY));
    };

    SelectObject(hdc, GetStockObject(DC_PEN));
    SetDCPenColor(hdc, RGB(0, 200, 100));
    for (const auto& conn : POSE_CONNECTIONS)
        draw_line(conn.first, conn.second);

    SelectObject(hdc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(hdc, RGB(255, 220, 0));
    SetDCPenColor(hdc, RGB(255, 220, 0));
    for (int i = 0; i < NUM_POSE_LANDMARKS; i++) {
        if (p.lm_x[i] == 0 && p.lm_y[i] == 0) continue;
        int cx = static_cast<int>(p.lm_x[i] * scaleX);
        int cy = static_cast<int>(p.lm_y[i] * scaleY);
        Ellipse(hdc, cx - 5, cy - 5, cx + 5, cy + 5);
    }
}

LRESULT CALLBACK StickManWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetBkMode(hdc, TRANSPARENT);
        if (g_authenticated) {
            std::vector<RealSenseID::PersonPose> poses;
            get_poses_copy(poses);
            draw_stick_man_gdi(hdc, poses);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            g_quit = true;
            PostQuitMessage(0);
        }
        return 0;
    case WM_CLOSE:
        g_quit = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool run_stick_man_window_win32() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StickManWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"SimonSaysStickMan";
    if (!RegisterClassExW(&wc)) return false;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Simon Says - Stick Man",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        POSE_WINDOW_W + 16, POSE_WINDOW_H + 39, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return false;

    ShowWindow(hwnd, SW_SHOW);
    SetTimer(hwnd, 1, 33, nullptr);  // ~30 fps redraw

    MSG msg;
    while (!g_quit && GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    KillTimer(hwnd, 1);
    return true;
}
#endif
#endif

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

#ifdef _WIN32
    SetConsoleCtrlHandler(ctrl_c_handler, TRUE);
#else
    signal(SIGINT, ctrl_c_handler);
#endif

    std::cout << "Simon Says starting... (Ctrl+C to exit)\n" << std::flush;
    std::cout << "Simon Says - RealSense ID\n";
    std::cout << "Only the enrolled player can make the stick man dance.\n\n" << std::flush;

    bool interactive = (ISATTY != 0);
    if (!interactive)
        std::cout << "(No console input - skipping enroll prompt, will try authenticate.)\n" << std::flush;

    std::cout << "Searching for RealSense ID device..." << std::flush;
    std::string port;
    RealSenseID::DeviceType device_type = RealSenseID::DeviceType::Unknown;
    if (!discover_rsid_device(port, device_type)) {
        std::cerr << "\nNo RealSense ID device found. Connect an F450/F460 or set RSID_PORT=COMx." << std::endl;
        return 1;
    }
    std::cout << " found " << RealSenseID::Description(device_type) << " on " << port << std::endl;

    RealSenseID::FaceAuthenticator authenticator(device_type);
    std::cout << "Connecting..." << std::flush;
    auto status = authenticator.Connect(get_serial_config(port.c_str()));
    if (status != RealSenseID::Status::Ok) {
        std::cerr << "Failed to connect: " << static_cast<int>(status) << std::endl;
        std::cerr << "Set RSID_PORT to your device port (e.g. COM9 on Windows)." << std::endl;
        return 1;
    }
    std::cout << " done.\n" << std::endl;

    g_authenticator_for_ctrl_c = &authenticator;

    // 1) Enroll if requested
    char choice = 'n';
    if (interactive) {
        std::cout << "Enroll a face? (y/n): " << std::flush;
        std::cin >> choice;
    }
    if (choice == 'y' || choice == 'Y') {
        EnrollCallback enroll_cb;
        std::cout << "Enrolling user '" << DEFAULT_USER_ID << "' - follow the pose hints.\n";
        status = authenticator.Enroll(enroll_cb, DEFAULT_USER_ID);
        if (status != RealSenseID::Status::Ok) {
            std::cerr << "Enroll failed (status " << static_cast<int>(status) << ")." << std::endl;
            if (status == RealSenseID::Status::Error) {
                std::cerr << "  The F460 may require secure (paired) mode for enrollment." << std::endl;
                std::cerr << "  Try: (1) Enroll using Intel's rsid-viewer first, or (2) Answer 'n' here and try Authenticate if you already have a user." << std::endl;
            }
            char cont = 'y';
            if (interactive) {
                std::cout << "Continue to authentication anyway? (y/n): " << std::flush;
                std::cin >> cont;
            }
            if (cont != 'y' && cont != 'Y') {
                g_authenticator_for_ctrl_c = nullptr;
                authenticator.Disconnect();
                return 1;
            }
            std::cout << std::endl;
        } else {
            std::cout << "Enrollment done.\n" << std::endl;
        }
    }

    // 2) Authenticate once (face recognition)
    std::cout << "Stand in front of the camera to authenticate..." << std::endl;
    RealSenseID::DeviceConfig dev_config;
    authenticator.QueryDeviceConfig(dev_config);
    dev_config.algo_flow = RealSenseID::DeviceConfig::AlgoFlow::All;
    authenticator.SetDeviceConfig(dev_config);

    AuthCallback auth_cb;
    status = authenticator.Authenticate(auth_cb);
    if (status != RealSenseID::Status::Ok) {
        std::cerr << "Authenticate call failed: " << static_cast<int>(status) << std::endl;
        g_authenticator_for_ctrl_c = nullptr;
        authenticator.Disconnect();
        return 1;
    }

    if (auth_cb.result != RealSenseID::AuthenticateStatus::Success) {
        std::cerr << "Authentication failed. Only enrolled users can play." << std::endl;
        g_authenticator_for_ctrl_c = nullptr;
        authenticator.Disconnect();
        return 1;
    }

    std::cout << "Authenticated as: " << auth_cb.authenticated_user_id << std::endl;
    g_authenticated = true;

    // 3) Switch to pose-only mode and run loop in background
    dev_config.algo_flow = RealSenseID::DeviceConfig::AlgoFlow::PoseEstimationOnly;
    authenticator.SetDeviceConfig(dev_config);

    PoseLoopCallback pose_cb;
    std::thread pose_thread([&]() {
        g_pose_loop_running = true;
        authenticator.AuthenticateLoop(pose_cb);
        g_pose_loop_running = false;
    });

#ifndef SIMONSAYS_NO_SDL
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!init_sdl(window, renderer)) {
        std::cerr << "SDL init failed; continuing without window." << std::endl;
    }

    while (!g_quit && window) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) g_quit = true;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) g_quit = true;
        }
        if (g_quit) break;

        SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
        SDL_RenderClear(renderer);

        if (g_authenticated) {
            std::vector<RealSenseID::PersonPose> poses;
            get_poses_copy(poses);
            draw_stick_man(renderer, poses);
        } else {
            // Locked: show message (no stick man)
            (void)renderer;
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(33); // ~30 fps
    }

    if (window) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
#else
#ifdef _WIN32
    if (!run_stick_man_window_win32())
        std::cerr << "Could not create stick man window." << std::endl;
#else
    std::cout << "Stick man window disabled (no SDL2). Press Enter to exit." << std::endl;
    std::cin.get();
    std::cin.get();
    g_quit = true;
#endif
#endif

    authenticator.Cancel();
    if (pose_thread.joinable()) pose_thread.join();
    g_authenticator_for_ctrl_c = nullptr;
    authenticator.Disconnect();
    std::cout << "Done." << std::endl;
    return 0;
}
