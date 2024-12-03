#include <iostream>
#include <stdio.h>
#include "openvr.h"
#include <chrono>
#include <thread>
#include "python_com.h"
#include <cmath>
#include <filesystem>

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

    VROverlayHandle_t handle;
    VROverlay()->CreateOverlay("EyeTrackVR", "Overlay", &handle);
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
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Pause for 10 milliseconds
        }

        if (Center_Only == true) {
            SendSock(Overlay_Calib_State);
            std::cout << "[INFO] Done!" << std::endl;
            return 0;
        }

        if (Overlay_Size <= 0.03) {
            std::cout << "[INFO] Calibrated point: ";
            std::cout << Overlay_Calib_State + 1 << std::endl;

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

            SendSock(Overlay_Calib_State);
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Pause for UDP delay
            Overlay_Size = 1.0;
        }
    }

    VROverlay()->DestroyOverlay(handle);
    std::cout << "[INFO] Done!" << std::endl;
    VR_Shutdown();
    return 0;
}