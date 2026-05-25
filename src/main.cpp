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
static constexpr uint32_t TEXT_H = 300;
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

    RECT full_rect = {0, 0, (LONG)TEXT_W, (LONG)TEXT_H};
    HBRUSH bg = CreateSolidBrush(RGB(15, 15, 25));
    FillRect(dc, &full_rect, bg);
    DeleteObject(bg);

    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);

    HFONT font = CreateFontW(
        -52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    HFONT old_font = (HFONT)SelectObject(dc, font);

    // Measure the text block height so we can vertically center it.
    RECT calc_rect = {0, 0, (LONG)TEXT_W, (LONG)TEXT_H};
    DrawTextW(dc, text.c_str(), -1, &calc_rect,
              DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
    int text_h   = calc_rect.bottom - calc_rect.top;
    int top      = ((int)TEXT_H - text_h) / 2;
    if (top < 0) top = 0;
    RECT draw_rect = {0, top, (LONG)TEXT_W, (LONG)TEXT_H};
    DrawTextW(dc, text.c_str(), -1, &draw_rect,
              DT_CENTER | DT_WORDBREAK);

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

// ── Data-collect / interactive overlay modes ──────────────────────────────────
//
// DC_POS: 9-point capture grid (cardinals ±1.1 m, corners ±0.85 m).
//
// interactive mode UDP protocol (port 2112 → Python, port 2113 ← Python):
//   Cmds received on 2113:
//     50    → show text overlay; bytes 4+ are the UTF-8 string to display
//     98    → hide text overlay
//     99    → hide dot
//     100   → normal gaze pass        (signals 0–8 + 19 on port 2112)
//     101   → squint pass             (signals 0–8 + 19 on port 2112)
//     102   → widen eyes pass         (signals 0–8 + 19 on port 2112)
//     103   → raise eyebrows fully    (signals 0–8 + 19 on port 2112)
//     104   → raise eyebrows halfway  (signals 0–8 + 19 on port 2112)
//     105   → lower eyebrows fully    (signals 0–8 + 19 on port 2112)
//     106   → lower eyebrows halfway  (signals 0–8 + 19 on port 2112)
//     200   → exit
//   Signals sent on 2112:
//     255   → overlay ready (sent once at startup)
//     0–8   → capture-ready at point N (during any pass)
//     19    → current pass complete

static constexpr uint16_t PORT_INTERACTIVE_CMD = 2113;

// Capture-phase grid: 4 cardinal extremes + 4 diagonal corners + center
static const float DC_POS[9][2] = {
    { 0.0f,   0.0f},   // 0  center
    {-1.1f,   0.0f},   // 1  left
    { 1.1f,   0.0f},   // 2  right
    { 0.0f,   1.1f},   // 3  up
    { 0.0f,  -1.1f},   // 4  down
    {-0.85f,  0.85f},  // 5  upper-left
    { 0.85f,  0.85f},  // 6  upper-right
    {-0.85f, -0.85f},  // 7  lower-left
    { 0.85f, -0.85f},  // 8  lower-right
};

// Expression-guidance grid (includes diagonals for top-left / bottom-right etc.)
static constexpr float EXPR_DOT_SIZE = 0.10f;
static const float EXPR_POS[9][2] = {
    { 0.0f,  0.0f},   // 0  center
    {-0.9f,  0.0f},   // 1  left
    { 0.9f,  0.0f},   // 2  right
    { 0.0f,  0.9f},   // 3  up
    { 0.0f, -0.9f},   // 4  down
    { 0.9f,  0.9f},   // 5  upper-right
    {-0.9f,  0.9f},   // 6  upper-left
    { 0.9f, -0.9f},   // 7  lower-right
    {-0.9f, -0.9f},   // 8  lower-left
};

static constexpr int   DC_TARGETS     = 9;
static constexpr float DC_DOT_LARGE   = 0.42f;
static constexpr float DC_DOT_SMALL   = 0.05f;
// 0.37 m over 100 x 5 ms = 0.5 s shrink; total per dot ~1.15 s
static constexpr float DC_SHRINK_STEP = 0.0037f;
static constexpr int   DC_SHRINK_MS   = 5;
static constexpr int   DC_APPEAR_MS   = 150;
static constexpr int   DC_HOLD_MS     = 500;
static constexpr int   DC_SQUINT_WAIT_MS = 5500;  // standalone datacollect mode only

static void run_dc_phase(VROverlayHandle_t dot_h, SOCKET sock,
                          const sockaddr_in& addr, int base_idx) {
    for (int i = 0; i < DC_TARGETS; ++i) {
        float px = DC_POS[i][0];
        float py = DC_POS[i][1] + Y_BIAS;

        VROverlay()->SetOverlayWidthInMeters(dot_h, DC_DOT_LARGE);
        set_overlay_transform(dot_h, px, py);
        std::this_thread::sleep_for(std::chrono::milliseconds(DC_APPEAR_MS));

        animate_shrink(dot_h, px, py, DC_DOT_LARGE, DC_DOT_SMALL,
                       DC_SHRINK_STEP, DC_SHRINK_MS);

        send_int32(sock, addr, base_idx + i);
        std::this_thread::sleep_for(std::chrono::milliseconds(DC_HOLD_MS));
    }
}

static int run_datacollect(VROverlayHandle_t dot_h, VROverlayHandle_t text_h,
                             SOCKET sock, const sockaddr_in& addr) {
    std::wstring cur_text;
    auto show_text = [&](const std::wstring& t) {
        if (t != cur_text && text_h != vr::k_ulOverlayHandleInvalid) {
            cur_text = t;
            update_text(text_h, t);
        }
    };

    std::cout << "[INFO] Data-collect 9-point overlay\n";

    show_text(L"Follow the purple dot");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    run_dc_phase(dot_h, sock, addr, 0);

    // Signal Python to announce squint phase, then wait for TTS to complete
    send_int32(sock, addr, 9);
    show_text(L"Squint while following the dot");
    std::this_thread::sleep_for(std::chrono::milliseconds(DC_SQUINT_WAIT_MS));

    run_dc_phase(dot_h, sock, addr, 10);

    send_int32(sock, addr, 19);
    show_text(L"Collection complete!");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    std::cout << "[INFO] Data-collect done\n";
    return 0;
}

// ── Interactive overlay mode (used by the data-collection app) ────────────────

static int run_interactive(VROverlayHandle_t dot_h, VROverlayHandle_t text_h,
                             SOCKET send_sock, const sockaddr_in& send_addr) {
    SOCKET recv_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (recv_sock == INVALID_SOCKET) {
        std::cout << "[ERROR] Failed to create command socket\n";
        return 1;
    }

    BOOL reuse = TRUE;
    setsockopt(recv_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in cmd_addr{};
    cmd_addr.sin_family = AF_INET;
    cmd_addr.sin_port   = htons(PORT_INTERACTIVE_CMD);
    if (inet_pton(AF_INET, "127.0.0.1", &cmd_addr.sin_addr) != 1
            || bind(recv_sock, (sockaddr*)&cmd_addr, sizeof(cmd_addr)) != 0) {
        std::cout << "[ERROR] Failed to bind command socket on port "
                  << PORT_INTERACTIVE_CMD << "\n";
        closesocket(recv_sock);
        return 1;
    }

    DWORD recv_ms = 100;
    setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recv_ms, sizeof(recv_ms));

    std::wstring cur_text;
    auto show_text = [&](const std::wstring& t) {
        if (t != cur_text && text_h != vr::k_ulOverlayHandleInvalid) {
            cur_text = t;
            update_text(text_h, t);
        }
    };

    std::cout << "[INFO] Interactive mode ready"
              << " (cmd=" << PORT_INTERACTIVE_CMD
              << " sig=" << PORT_CLASSIC << ")\n";

    VROverlay()->HideOverlay(dot_h);
    if (text_h != vr::k_ulOverlayHandleInvalid) VROverlay()->HideOverlay(text_h);

    // Signal Python we are ready
    send_int32(send_sock, send_addr, 255);

    int result = 0;
    bool running = true;
    while (running) {
        char recv_buf[512];
        int len = recvfrom(recv_sock, recv_buf, (int)sizeof(recv_buf) - 1, 0, nullptr, nullptr);

        if (len == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) continue;
            std::cout << "[ERROR] recvfrom: " << err << "\n";
            result = 1;
            break;
        }
        if (len < 4) continue;

        int32_t cmd = ntohl(*reinterpret_cast<const int32_t*>(recv_buf));

        if (cmd == 50 && len > 4) {
            // Python is sending a UTF-8 prompt string to display in the text overlay
            int wlen = MultiByteToWideChar(CP_UTF8, 0, recv_buf + 4, len - 4, nullptr, 0);
            if (wlen > 0) {
                std::wstring wide(wlen, 0);
                MultiByteToWideChar(CP_UTF8, 0, recv_buf + 4, len - 4, &wide[0], wlen);
                show_text(wide);
                if (text_h != vr::k_ulOverlayHandleInvalid) VROverlay()->ShowOverlay(text_h);
            }
        } else if (cmd == 98) {
            if (text_h != vr::k_ulOverlayHandleInvalid) VROverlay()->HideOverlay(text_h);
            cur_text.clear();  // force re-render next time
        } else if (cmd == 99) {
            VROverlay()->HideOverlay(dot_h);
        } else if (cmd >= 100 && cmd <= 106) {
            static const wchar_t* const kPassText[] = {
                L"Follow the dot",                             // 100
                L"Squint\nfollow the dot",                     // 101
                L"Widen eyes\nfollow the dot",                 // 102
                L"Raise eyebrows fully\nfollow the dot",       // 103
                L"Raise eyebrows halfway\nfollow the dot",     // 104
                L"Lower eyebrows fully\nfollow the dot",       // 105
                L"Lower eyebrows halfway\nfollow the dot",     // 106
            };
            if (text_h != vr::k_ulOverlayHandleInvalid) VROverlay()->ShowOverlay(text_h);
            show_text(kPassText[cmd - 100]);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            VROverlay()->ShowOverlay(dot_h);
            run_dc_phase(dot_h, send_sock, send_addr, 0);
            send_int32(send_sock, send_addr, 19);
            VROverlay()->HideOverlay(dot_h);
            if (text_h != vr::k_ulOverlayHandleInvalid) VROverlay()->HideOverlay(text_h);
            cur_text.clear();
        } else if (cmd == 200) {
            running = false;
        }
    }

    closesocket(recv_sock);
    return result;
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
    bool center_only       = false;
    bool ellipse_mode      = false;
    bool datacollect_mode  = false;
    bool interactive_mode  = false;

    if (argc > 1) {
        std::string arg(argv[1]);
        if (arg == "center")            center_only      = true;
        else if (arg == "ellipse")      ellipse_mode     = true;
        else if (arg == "datacollect")  datacollect_mode = true;
        else if (arg == "interactive")  interactive_mode = true;
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
    if (ellipse_mode)           std::cout << "ellipse";
    else if (center_only)       std::cout << "center";
    else if (datacollect_mode)  std::cout << "datacollect";
    else if (interactive_mode)  std::cout << "interactive";
    else                        std::cout << "9-point";
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

    // Text overlay (ellipse, datacollect, and interactive modes)
    VROverlayHandle_t text_h = vr::k_ulOverlayHandleInvalid;
    if (ellipse_mode || datacollect_mode || interactive_mode) {
        if (VROverlay()->CreateOverlay("EyeTrackVR.text", "Instructions", &text_h)
                != vr::VROverlayError_None) {
            std::cout << "[WARN] Could not create text overlay — continuing without it\n";
        } else {
            VROverlay()->SetOverlayWidthInMeters(text_h, 1.5f);
            set_overlay_transform(text_h, 0.0f, -0.55f + Y_BIAS);
            update_text(text_h, ellipse_mode ? L"Preparing calibration..." : L"Preparing...");
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
    else if (datacollect_mode)
        result = run_datacollect(dot_h, text_h, sock, addr);
    else if (interactive_mode)
        result = run_interactive(dot_h, text_h, sock, addr);
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
