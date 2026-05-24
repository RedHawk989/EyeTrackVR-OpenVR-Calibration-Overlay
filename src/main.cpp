// winsock2.h must precede windows.h to avoid redefinition errors
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iostream>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>
#include <filesystem>
#include <string>
#include <vector>

#include "openvr.h"

// cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/Users/beaul/vcpkg/scripts/buildsystems/vcpkg.cmake
// cmake --build build

using namespace vr;
namespace fs = std::filesystem;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Port constant ─────────────────────────────────────────────────────────────

static constexpr uint16_t PORT_CLASSIC = 2112;

// Shift the entire calibration scene downward so it aligns with typical
// straight-ahead gaze rather than the HMD's optical centre.
static constexpr float Y_BIAS = -0.10f;

// Express targets: normalised [-1,1] position scaled by OVERLAY_SCALE to metres.
static constexpr float OVERLAY_SCALE = 0.82f;
static const float EXPRESS_POS[9][2] = {
    { 0.0f,  0.0f},   // 0 center
    { 0.0f,  1.0f},   // 1 top
    { 1.0f,  0.0f},   // 2 right
    { 0.0f, -1.0f},   // 3 bottom
    {-1.0f,  0.0f},   // 4 left
    { 1.0f,  1.0f},   // 5 upper-right
    {-1.0f,  1.0f},   // 6 upper-left
    { 1.0f, -1.0f},   // 7 lower-right
    {-1.0f, -1.0f},   // 8 lower-left
};
static const wchar_t* EXPRESS_HINTS[9] = {
    L"Look at the dot  —  center",
    L"Look at the dot  —  up",
    L"Look at the dot  —  right",
    L"Look at the dot  —  down",
    L"Look at the dot  —  left",
    L"Look at the dot  —  upper right",
    L"Look at the dot  —  upper left",
    L"Look at the dot  —  lower right",
    L"Look at the dot  —  lower left",
};
static constexpr int NUM_EXPRESS_TARGETS = 9;

// ── Text overlay (GDI-rendered RGBA) ─────────────────────────────────────────

static constexpr uint32_t TEXT_W = 1024;
static constexpr uint32_t TEXT_H = 128;
static std::vector<uint8_t> g_text_pixels;

static void update_text(VROverlayHandle_t handle, const std::wstring& text) {
    if (handle == vr::k_ulOverlayHandleInvalid) return;
    if (g_text_pixels.size() != TEXT_W * TEXT_H * 4)
        g_text_pixels.resize(TEXT_W * TEXT_H * 4, 0);

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = (LONG)TEXT_W;
    bmi.bmiHeader.biHeight      = -(LONG)TEXT_H; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void*   bits = nullptr;
    HBITMAP hbm  = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbm) { DeleteDC(dc); return; }
    HBITMAP old_bm = (HBITMAP)SelectObject(dc, hbm);

    RECT rect = {0, 0, (LONG)TEXT_W, (LONG)TEXT_H};
    HBRUSH bg = CreateSolidBrush(RGB(15, 15, 25));
    FillRect(dc, &rect, bg);
    DeleteObject(bg);

    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);

    HFONT font = CreateFontW(
        -56, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    HFONT old_font = (HFONT)SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), -1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const uint8_t* src = reinterpret_cast<const uint8_t*>(bits);
    for (uint32_t i = 0; i < TEXT_W * TEXT_H; ++i) {
        g_text_pixels[i*4 + 0] = src[i*4 + 2]; // R
        g_text_pixels[i*4 + 1] = src[i*4 + 1]; // G
        g_text_pixels[i*4 + 2] = src[i*4 + 0]; // B
        g_text_pixels[i*4 + 3] = 230;           // A
    }

    SelectObject(dc, old_font); DeleteObject(font);
    SelectObject(dc, old_bm);   DeleteObject(hbm);
    DeleteDC(dc);

    VROverlay()->SetOverlayRaw(handle, g_text_pixels.data(), TEXT_W, TEXT_H, 4);
}

// ── OpenVR overlay helpers ────────────────────────────────────────────────────

static void set_overlay_transform(VROverlayHandle_t h, float x, float y, float z = -2.0f) {
    vr::HmdMatrix34_t m = {
        1.0f, 0.0f, 0.0f, x,
        0.0f, 1.0f, 0.0f, y,
        0.0f, 0.0f, 1.0f, z
    };
    VROverlay()->SetOverlayTransformTrackedDeviceRelative(
        h, vr::k_unTrackedDeviceIndex_Hmd, &m);
}

static void animate_shrink(VROverlayHandle_t h, float x, float y,
                             float from_size, float to_size, float step,
                             int step_ms = 5) {
    float s = from_size;
    while (s > to_size) {
        s = std::max(s - step, to_size);
        VROverlay()->SetOverlayWidthInMeters(h, s);
        set_overlay_transform(h, x, y);
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }
}

// ── UDP send helpers ──────────────────────────────────────────────────────────

static void udp_send(SOCKET sock, const sockaddr_in& addr,
                      const void* buf, int len) {
    if (sendto(sock, (const char*)buf, len, 0,
               (const sockaddr*)&addr, sizeof(addr)) != len)
        std::cout << "[WARN] UDP send failed\n";
}

static void send_int32(SOCKET sock, const sockaddr_in& addr, int32_t v) {
    int32_t nv = htonl(v);
    udp_send(sock, addr, &nv, 4);
}

// ── Classic 9-point / center-only mode ───────────────────────────────────────

static int run_classic(VROverlayHandle_t dot_h, SOCKET sock,
                        const sockaddr_in& addr, bool center_only) {
    float size  = center_only ? 2.5f : 1.0f;
    float pos_x = center_only ? 0.0f : 0.8f;
    float pos_y = center_only ? 0.0f : 0.8f;
    int   state = 0;

    std::cout << "[INFO] Classic calibration ("
              << (center_only ? "center" : "9-point") << ")\n";

    while (state < 9) {
        while (size > 0.03f) {
            size -= 0.01f;
            VROverlay()->SetOverlayWidthInMeters(dot_h, size);
            set_overlay_transform(dot_h, pos_x, pos_y);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (center_only) {
            send_int32(sock, addr, 9);
            return 0;
        }

        std::cout << "[INFO] Point " << state + 1 << "\n";
        send_int32(sock, addr, state);
        ++state;

        if (pos_x <= 0.9f && pos_x >= 0.0f) {
            pos_x -= 0.8f;
            if (std::fabs(pos_x) < 1.3e-7f) pos_x = 0.0f;
        } else {
            pos_x = 0.8f;
        }
        if (state % 3 == 0) {
            if (pos_y <= 0.9f && pos_y >= 0.0f) {
                pos_y -= 0.8f;
                if (std::fabs(pos_y) < 1.3e-7f) pos_y = 0.0f;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        size = 1.0f;
    }
    send_int32(sock, addr, 9);
    std::cout << "[INFO] Classic cal done\n";
    return 0;
}

// ── Overlay ellipse calibration ───────────────────────────────────────────────
//
// The dot starts at the outer edge of the gaze range (OVERLAY_SCALE radius)
// and spirals inward over SPIRAL_S seconds while completing SPIRAL_ROTS full
// rotations.  The dot size shrinks linearly from DOT_LARGE to DOT_SMALL.
//
// Protocol (port 2112, int32 big-endian):
//   int32(0)  → Python starts collecting raw pupil samples
//   int32(9)  → Python fits and saves the ellipse
//
// Python accumulates every frame during the spiral, giving ~600+ uniformly
// distributed samples that span the full gaze range — much better than
// sparse static grabs for a std-deviation-based ellipse fit.

static int run_ellipse(VROverlayHandle_t dot_h, VROverlayHandle_t text_h,
                        SOCKET sock, const sockaddr_in& addr) {
    static constexpr int    FPS         = 60;
    static constexpr double SPIRAL_S    = 13.0;   // spiral duration (seconds)
    static constexpr double SPIRAL_ROTS = 4.0;    // full rotations while spiraling in
    static constexpr float  SPIRAL_RX   = 1.40f;  // horizontal starting radius (m) at z = -2 m
    static constexpr float  SPIRAL_RY   = 1.20f;  // vertical starting radius (m) at z = -2 m
    static constexpr float  DOT_LARGE   = 0.12f;  // dot size at outer edge (m)
    static constexpr float  DOT_SMALL   = 0.04f;  // dot size at center (m)
    static constexpr int    CENTER_MS   = 1500;   // hold at center after spiral

    const auto frame_us = std::chrono::microseconds(1000000 / FPS);

    // Only call update_text when the string actually changes — prevents per-frame
    // GDI re-renders which cause visible flickering on the text overlay.
    std::wstring cur_text;
    auto show_text = [&](const std::wstring& t) {
        if (t != cur_text && text_h != vr::k_ulOverlayHandleInvalid) {
            cur_text = t;
            update_text(text_h, t);
        }
    };

    std::cout << "[INFO] Ellipse calibration — spiral in ("
              << SPIRAL_S << "s, " << SPIRAL_ROTS << " rotations, RX=" << SPIRAL_RX
              << "m RY=" << SPIRAL_RY << "m)\n";

    // Appear at start position (outer right) before sampling begins.
    // Y_BIAS shifts the whole scene down to match natural straight-ahead gaze.
    VROverlay()->SetOverlayWidthInMeters(dot_h, DOT_LARGE);
    set_overlay_transform(dot_h, SPIRAL_RX, Y_BIAS);
    show_text(L"Follow the dot -- keep your gaze on it");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    // Signal Python: start collecting
    send_int32(sock, addr, 0);
    Beep(880, 100);

    // ── Spiral inward ─────────────────────────────────────────────────────────
    auto t0 = std::chrono::steady_clock::now();
    int last_secs = -1;
    while (true) {
        auto   now  = std::chrono::steady_clock::now();
        double t    = std::chrono::duration<double>(now - t0).count();
        if (t >= SPIRAL_S) break;

        double frac  = t / SPIRAL_S;
        double shrink = 1.0 - frac;
        double angle = 2.0 * M_PI * SPIRAL_ROTS * frac;

        float nx = SPIRAL_RX * (float)(shrink * std::cos(angle));
        float ny = SPIRAL_RY * (float)(shrink * std::sin(angle)) + Y_BIAS;
        float dot_sz = DOT_LARGE + (DOT_SMALL - DOT_LARGE) * (float)frac;

        VROverlay()->SetOverlayWidthInMeters(dot_h, dot_sz);
        set_overlay_transform(dot_h, nx, ny);

        int secs_left = (int)(SPIRAL_S - t) + 1;
        if (secs_left <= 5 && secs_left != last_secs) {
            last_secs = secs_left;
            show_text(L"Almost done... " + std::to_wstring(secs_left) + L"s");
        }

        std::this_thread::sleep_for(frame_us);
    }

    // ── Hold at center ────────────────────────────────────────────────────────
    VROverlay()->SetOverlayWidthInMeters(dot_h, DOT_SMALL);
    set_overlay_transform(dot_h, 0.0f, Y_BIAS);
    show_text(L"Hold -- look at center");
    std::this_thread::sleep_for(std::chrono::milliseconds(CENTER_MS));

    // Signal Python: fit and save
    send_int32(sock, addr, 9);
    show_text(L"Calibration complete!");
    Beep(1000, 400);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << "[INFO] Ellipse done\n";
    return 0;
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    bool center_only  = false;
    bool ellipse_mode = false;

    if (argc > 1) {
        std::string arg(argv[1]);
        if (arg == "center")       center_only  = true;
        else if (arg == "ellipse") ellipse_mode = true;
    }

    // ── Winsock ───────────────────────────────────────────────────────────────
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "[ERROR] WSAStartup failed\n";
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cout << "[ERROR] Failed to create UDP socket\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT_CLASSIC);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        std::cout << "[ERROR] Invalid address\n";
        closesocket(sock); WSACleanup();
        return 1;
    }

    std::cout << "EyeTrackVR OpenVR Calibration Overlay — ";
    if (ellipse_mode)     std::cout << "ellipse";
    else if (center_only) std::cout << "center";
    else                  std::cout << "9-point";
    std::cout << " (udp://127.0.0.1:" << PORT_CLASSIC << ")\n";

    // ── OpenVR ────────────────────────────────────────────────────────────────
    const fs::path imgPath = fs::absolute("assets/Purple_Dot.png");
    if (!fs::exists(imgPath)) {
        std::cout << "[ERROR] Image not found: " << imgPath << "\n";
        closesocket(sock); WSACleanup();
        return 1;
    }

    EVRInitError vr_err;
    VR_Init(&vr_err, vr::VRApplication_Overlay);
    if (vr_err != 0) {
        std::cout << "[ERROR] OpenVR: " << VR_GetVRInitErrorAsSymbol(vr_err) << "\n";
        closesocket(sock); WSACleanup();
        return 1;
    }

    // Dot overlay
    VROverlayHandle_t dot_h = vr::k_ulOverlayHandleInvalid;
    if (VROverlay()->CreateOverlay("EyeTrackVR.dot", "Dot", &dot_h) != vr::VROverlayError_None) {
        std::cout << "[ERROR] Failed to create dot overlay\n";
        VR_Shutdown(); closesocket(sock); WSACleanup();
        return 1;
    }
    VROverlay()->SetOverlayFromFile(dot_h, imgPath.string().c_str());
    VROverlay()->SetOverlayWidthInMeters(dot_h, 2.0f);
    set_overlay_transform(dot_h, 0.0f, 0.0f);
    VROverlay()->ShowOverlay(dot_h);

    // Text overlay (ellipse mode only)
    VROverlayHandle_t text_h = vr::k_ulOverlayHandleInvalid;
    if (ellipse_mode) {
        if (VROverlay()->CreateOverlay("EyeTrackVR.text", "Instructions", &text_h)
                != vr::VROverlayError_None) {
            std::cout << "[WARN] Could not create text overlay — continuing without it\n";
        } else {
            VROverlay()->SetOverlayWidthInMeters(text_h, 1.2f);
            set_overlay_transform(text_h, 0.0f, -0.45f + Y_BIAS);
            update_text(text_h, L"Preparing calibration...");
            VROverlay()->SetOverlaySortOrder(text_h, 0);
            VROverlay()->ShowOverlay(text_h);
        }
    }
    // Dot always renders on top of text.
    VROverlay()->SetOverlaySortOrder(dot_h, 1);

    // ── Run mode ──────────────────────────────────────────────────────────────
    int result = 0;
    if (ellipse_mode)
        result = run_ellipse(dot_h, text_h, sock, addr);
    else
        result = run_classic(dot_h, sock, addr, center_only);

    // ── Cleanup ───────────────────────────────────────────────────────────────
    if (text_h != vr::k_ulOverlayHandleInvalid) {
        VROverlay()->HideOverlay(text_h);
        VROverlay()->DestroyOverlay(text_h);
    }
    VROverlay()->HideOverlay(dot_h);
    VROverlay()->DestroyOverlay(dot_h);
    VR_Shutdown();
    closesocket(sock);
    WSACleanup();
    return result;
}
