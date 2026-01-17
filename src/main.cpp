#include <iostream>
#include <stdio.h>
#include "openvr.h"
#include <chrono>
#include <thread>
#include <cmath>
#include <filesystem>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

// For my own reference when I come back to this a year later
// cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/Users/beaul/vcpkg/scripts/buildsystems/vcpkg.cmake
// make --build build

using namespace vr;
namespace fs = std::filesystem;

bool Center_Only = false;
float Overlay_Size = 1.0f;
float Overlay_X_Pos = 0.8f;
float Overlay_Y_Pos = 0.8f;
float Overlay_Z_Pos = 0.0f;
int Overlay_Calib_State = 0;

int main(int argc, char **argv)
{

    const std::string endpoint_host = "127.0.0.1";
    const uint16_t endpoint_port = 2112;

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cout << "[ERROR] WSAStartup failed." << std::endl;
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cout << "[ERROR] Failed to create UDP socket." << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in endpoint_addr{};
    endpoint_addr.sin_family = AF_INET;
    endpoint_addr.sin_port = htons(endpoint_port);
    if (inet_pton(AF_INET, endpoint_host.c_str(), &endpoint_addr.sin_addr) != 1) {
        std::cout << "[ERROR] Invalid endpoint address." << std::endl;
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    auto send_int = [&](int32_t value) {
        int32_t network_value = htonl(value);
        int sent = sendto(
            sock,
            reinterpret_cast<const char*>(&network_value),
            sizeof(network_value),
            0,
            reinterpret_cast<const sockaddr*>(&endpoint_addr),
            sizeof(endpoint_addr)
        );
        if (sent != sizeof(network_value)) {
            std::cout << "[WARN] Failed to send calibration message." << std::endl;
        }
    };

    std::cout << "Sending to udp://" << endpoint_host << ":" << endpoint_port << std::endl;
    std::cout << "Welcome to the EyeTrackVR OpenVR Calibration Overlay!" << std::endl;

    if (argc > 1 && std::string(argv[1]) == "center")
    {
        std::cout << "[INFO] Calibrate Center Point Only:" << std::endl;
        Center_Only = true;
        Overlay_X_Pos = 0.0;
        Overlay_Y_Pos = 0.0;
        Overlay_Size = 2.5;
    }

    const fs::path imagePath = fs::absolute("assets/Purple_Dot.png");
    std::cout << "[INFO] Loading image: " << imagePath << std::endl;
    if (!fs::exists(imagePath)) {
        std::cout << "[ERROR] Image not found: " << imagePath << std::endl;
        return 1;
    }

    EVRInitError error;
    VR_Init(&error, vr::VRApplication_Overlay);
    if (error != 0) {
        printf("error %s\n", VR_GetVRInitErrorAsSymbol(error));
        return 1;
    }
    bool vr_initialized = true;
    bool wsa_initialized = true;

    VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
    bool overlay_created = false;
    auto cleanup = [&]() {
        if (overlay_created) {
            VROverlay()->HideOverlay(handle);
            VROverlay()->DestroyOverlay(handle);
            overlay_created = false;
        }
        if (vr_initialized) {
            VR_Shutdown();
            vr_initialized = false;
        }
        if (wsa_initialized) {
            closesocket(sock);
            WSACleanup();
            wsa_initialized = false;
        }
    };

    if (VROverlay()->CreateOverlay("EyeTrackVR", "Overlay", &handle) != vr::VROverlayError_None) {
        std::cout << "[ERROR] Failed to create overlay." << std::endl;
        cleanup();
        return 1;
    }
    overlay_created = true;
    VROverlay()->SetOverlayFromFile(handle, imagePath.string().c_str());
    VROverlay()->SetOverlayWidthInMeters(handle, 2);
    VROverlay()->ShowOverlay(handle);

    std::cout << "[INFO] Calibrating..." << std::endl;
    while (Overlay_Calib_State < 9) {
        // animation
        while (Overlay_Size > 0.03) {
            Overlay_Size -= 0.01f;
            VROverlay()->SetOverlayWidthInMeters(handle, Overlay_Size);

            vr::HmdMatrix34_t transform = {
                1.0f, 0.0f, 0.0f, Overlay_X_Pos,
                0.0f, 1.0f, 0.0f, Overlay_Y_Pos,
                0.0f, 0.0f, 1.0f, -2.0f
            };

            VROverlay()->SetOverlayTransformTrackedDeviceRelative(handle, vr::k_unTrackedDeviceIndex_Hmd, &transform);
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Pause for 1 milliseconds
        }

        if (Center_Only == true) {
            // Send completion for center-only (Python expects int 9)
            send_int(9);

            std::cout << "[INFO] Done!" << std::endl;
            cleanup();
            return 0;
        }

        if (Overlay_Size <= 0.03) {
            std::cout << "[INFO] Calibrated point: ";
            std::cout << Overlay_Calib_State + 1 << std::endl;

            // Send the calibration state as int
            send_int(Overlay_Calib_State);

            Overlay_Calib_State++;
            if (Overlay_X_Pos <= 0.9 && Overlay_X_Pos >= 0.0) {
                Overlay_X_Pos = Overlay_X_Pos - 0.8f;
                if (fabs(Overlay_X_Pos) < 1.3e-7)
                {
                    Overlay_X_Pos = 0.0;
                };
            }
            else {
                Overlay_X_Pos = 0.8f;
            };

            if (Overlay_Calib_State % 3 == 0) {
                if (Overlay_Y_Pos <= 0.9 && Overlay_Y_Pos >= 0.0) {
                    Overlay_Y_Pos = Overlay_Y_Pos - 0.8f;
                    if (fabs(Overlay_Y_Pos) < 1.3e-7) {
                        Overlay_Y_Pos = 0.0;
                    };
                };
            };

            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Pause for UDP delay
            Overlay_Size = 1.0;
        }
    }

    // Signal completion to the Python app (expects int 9).
    send_int(9);

    std::cout << "[INFO] Done!" << std::endl;
    cleanup();
    return 0;
}