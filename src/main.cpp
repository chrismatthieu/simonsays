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
#ifdef RSID_SECURE
#include "secure_mode_helper.h"
#include <fstream>
#endif
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

static std::string g_serial_port_storage;

RealSenseID::SerialConfig get_serial_config(const char* port) {
    g_serial_port_storage = port;
    RealSenseID::SerialConfig c;
    c.port = g_serial_port_storage.c_str();
    return c;
}

#ifdef RSID_SECURE
constexpr size_t RSID_DEVICE_PUBKEY_SIZE = 64;
const char* RSID_DEVICE_KEY_FILE = ".rsid_device_key";

bool load_device_pubkey(std::vector<unsigned char>& out_key) {
    std::ifstream f(RSID_DEVICE_KEY_FILE, std::ios::binary);
    if (!f) return false;
    out_key.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return out_key.size() == RSID_DEVICE_PUBKEY_SIZE;
}

bool save_device_pubkey(const unsigned char* key) {
    std::ofstream f(RSID_DEVICE_KEY_FILE, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(key), RSID_DEVICE_PUBKEY_SIZE);
    return f.good();
}

bool do_pair(RealSenseID::FaceAuthenticator& authenticator, RealSenseID::Samples::SignHelper& signer) {
    const unsigned char* host_pub = signer.GetHostPubKey();
    unsigned char host_sig[32] = {0};
    if (!signer.Sign(host_pub, 64, host_sig)) {
        std::cerr << "Failed to sign host public key." << std::endl;
        return false;
    }
    char device_pubkey[64] = {0};
    auto st = authenticator.Pair(reinterpret_cast<const char*>(host_pub), reinterpret_cast<const char*>(host_sig), device_pubkey);
    if (st != RealSenseID::Status::Ok) {
        std::cerr << "Pair failed: " << static_cast<int>(st) << std::endl;
        return false;
    }
    signer.UpdateDevicePubKey(reinterpret_cast<unsigned char*>(device_pubkey));
    if (!save_device_pubkey(reinterpret_cast<unsigned char*>(device_pubkey))) {
        std::cerr << "Warning: could not save device key to " << RSID_DEVICE_KEY_FILE << std::endl;
    } else {
        std::cout << "Paired and saved device key to " << RSID_DEVICE_KEY_FILE << std::endl;
    }
    return true;
}
#endif

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

// ---- Pose loop callback: only receives pose (AlgoFlow::PoseEstimationOnly). g_authenticated is updated by periodic re-auth. ----
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
    window = SDL_CreateWindow("Simon Says - Can you make the stick man Dance?",
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
        // Title on top so it is never covered by the stick man
        RECT textRect = { 0, 4, rc.right, 44 };
        SetTextColor(hdc, RGB(220, 255, 220));
        HFONT font = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            FF_DONTCARE, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(hdc, font);
        DrawTextW(hdc, L"Can you make the stick man Dance?", -1, &textRect, DT_CENTER | DT_TOP | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(font);
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

#ifdef RSID_SECURE
    // This SDK version does not support secure (pairing) mode for F46x — only F45x.
    if (device_type == RealSenseID::DeviceType::F46x) {
        std::cerr << "This build uses secure mode (RSID_SECURE), which is not supported for F46x in this SDK." << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  1) Build without secure: unset SIMONSAYS_SECURE and run build.cmd; enroll with Intel RealSense ID Viewer, then run Simon Says and answer n to enroll." << std::endl;
        std::cerr << "  2) Use an F45x device for in-app pairing and enrollment with this secure build." << std::endl;
        return 1;
    }
    std::cout << "Secure mode: creating SignHelper..." << std::flush;
    RealSenseID::Samples::SignHelper signer;
    std::cout << " OK." << std::endl;
    bool need_pair = true;
    std::vector<unsigned char> saved_device_key;
    if (load_device_pubkey(saved_device_key)) {
        signer.UpdateDevicePubKey(saved_device_key.data());
        std::cout << "Loaded device key from " << RSID_DEVICE_KEY_FILE << std::endl;
        need_pair = false;
    }
    std::cout << "Creating authenticator (secure)..." << std::flush;
    RealSenseID::FaceAuthenticator authenticator(&signer, device_type);
    std::cout << " OK." << std::endl;
#else
    RealSenseID::FaceAuthenticator authenticator(device_type);
#endif
    std::cout << "Connecting..." << std::flush;
    auto status = authenticator.Connect(get_serial_config(port.c_str()));
    if (status != RealSenseID::Status::Ok) {
        std::cerr << "Failed to connect: " << static_cast<int>(status) << std::endl;
        std::cerr << "Set RSID_PORT to your device port (e.g. COM9 on Windows)." << std::endl;
        return 1;
    }
    std::cout << " done.\n" << std::endl;

#ifdef RSID_SECURE
    if (need_pair) {
        std::cout << "No device key found. Pairing with device..." << std::endl;
        if (!do_pair(authenticator, signer)) {
            std::cerr << "Pairing failed. Unpair the device in rsid-viewer if needed, then retry." << std::endl;
            authenticator.Disconnect();
            return 1;
        }
    }
#endif

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

    // 3) Pose stream (PoseEstimationOnly) for smooth stick man; re-auth every 10 s so mask/wrong person stops it
    constexpr int REAUTH_INTERVAL_SEC = 10;
    dev_config.algo_flow = RealSenseID::DeviceConfig::AlgoFlow::PoseEstimationOnly;
    authenticator.SetDeviceConfig(dev_config);

    PoseLoopCallback pose_cb;
    std::thread pose_thread;
    std::mutex pose_thread_mutex;

    auto run_pose_loop = [&]() {
        g_pose_loop_running = true;
        authenticator.AuthenticateLoop(pose_cb);
        g_pose_loop_running = false;
    };

    std::thread reauth_thread([&]() {
        while (!g_quit) {
            for (int i = 0; i < REAUTH_INTERVAL_SEC && !g_quit; ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
            if (g_quit) break;
            std::lock_guard<std::mutex> lock(pose_thread_mutex);
            authenticator.Cancel();
            if (pose_thread.joinable()) pose_thread.join();
            if (g_quit) break;
            dev_config.algo_flow = RealSenseID::DeviceConfig::AlgoFlow::All;
            authenticator.SetDeviceConfig(dev_config);
            AuthCallback reauth_cb;
            authenticator.Authenticate(reauth_cb);
            g_authenticated = (reauth_cb.result == RealSenseID::AuthenticateStatus::Success);
            dev_config.algo_flow = RealSenseID::DeviceConfig::AlgoFlow::PoseEstimationOnly;
            authenticator.SetDeviceConfig(dev_config);
            pose_thread = std::thread(run_pose_loop);
        }
    });

    {
        std::lock_guard<std::mutex> lock(pose_thread_mutex);
        pose_thread = std::thread(run_pose_loop);
    }

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

    g_quit = true;
    authenticator.Cancel();
    if (reauth_thread.joinable()) reauth_thread.join();
    {
        std::lock_guard<std::mutex> lock(pose_thread_mutex);
        if (pose_thread.joinable()) pose_thread.join();
    }
    g_authenticator_for_ctrl_c = nullptr;
    authenticator.Disconnect();
    std::cout << "Done." << std::endl;
    return 0;
}
