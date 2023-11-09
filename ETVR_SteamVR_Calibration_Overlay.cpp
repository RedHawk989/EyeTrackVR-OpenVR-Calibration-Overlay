#include <iostream>
#include <stdio.h>
#include "openvr.h"
#include <chrono>
#include <thread>
#include "Python_Com.h"
#include <cmath>


using namespace vr;

int Overlay_Calib_State = 0;
float Overlay_Size = 1.0;
float Overlay_X_Pos = 0.8;
float Overlay_Y_Pos = 0.8;
float Overlay_Z_Pos = 0.0;
bool Calibrate = true;
bool Center_Only = false;

void check_error(int line, EVRInitError error) { if (error != 0) printf("%d: error %s\n", line, VR_GetVRInitErrorAsSymbol(error)); }

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::cout << "Welcome to the EyeTrackVR OpenVR Calibration Overlay!" << std::endl;

    if (argc > 1 && std::string(argv[1]) == "center") {
        std::cout << "[INFO] Calibrate Center Point Only:" << std::endl;
        Center_Only = true;
        Overlay_X_Pos = 0.0;
        Overlay_Y_Pos = 0.0;
        Overlay_Size = 2.5;

    }
    EVRInitError error;
    VR_Init(&error, vr::VRApplication_Overlay);
    check_error(__LINE__, error);

    VROverlayHandle_t handle;
    std::cout << "[INFO] Calibrating..." << std::endl;

    VROverlay()->CreateOverlay("image", "image", &handle); /* key has to be unique, name doesn't matter */
    VROverlay()->SetOverlayFromFile(handle, "C:/Users/beaul/Downloads/Purple_Dot.png"); // we need to bundle this image or use relitive path not fixed path.
    VROverlay()->SetOverlayWidthInMeters(handle, 2);
    VROverlay()->ShowOverlay(handle);
    TrackedDevicePose_t trackedDevicePose[1];

    while (true) {
        while (Calibrate) {
            while (Overlay_Size > 0.03) {
                Overlay_Size -= 0.01;
                VROverlay()->SetOverlayWidthInMeters(handle, Overlay_Size);

                vr::HmdMatrix34_t transform = {
                    1.0f, 0.0f, 0.0f, Overlay_X_Pos,
                    0.0f, 1.0f, 0.0f, Overlay_Y_Pos,
                    0.0f, 0.0f, 1.0f, -2.0f
                };

                VROverlay()->SetOverlayTransformTrackedDeviceRelative(handle, vr::k_unTrackedDeviceIndex_Hmd, &transform);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Pause for 1 milliseconds
            }

            if (Center_Only == true) {
                std::cout << "[INFO] Done!" << std::endl;
                return 0;
            }

            if (Overlay_Size <= 0.03) {
                std::cout << "[INFO] Calibrated point: ";
                std::cout << Overlay_Calib_State + 1 << std::endl;
            /*
               std::cout << Overlay_X_Pos;
               std::cout << Overlay_Y_Pos;
               std::cout << "\n";
            */
                Overlay_Calib_State++;
                if (Overlay_X_Pos <= 0.9 && Overlay_X_Pos >= 0.0) {
                    Overlay_X_Pos = Overlay_X_Pos - 0.8;
                    if (fabs(Overlay_X_Pos) < 1.3e-7) {
                        Overlay_X_Pos = 0.0;
                    };
                }
                else {
                    Overlay_X_Pos = 0.8;
                };

                if (Overlay_Calib_State % 3 == 0) {
                    if (Overlay_Y_Pos <= 0.9 && Overlay_Y_Pos >= 0.0) {
                        Overlay_Y_Pos = Overlay_Y_Pos - 0.8;
                        if (fabs(Overlay_Y_Pos) < 1.3e-7) {
                            Overlay_Y_Pos = 0.0;
                        };
                    };
                };

                if (Overlay_Calib_State == 9) {
                    Calibrate = false;
                    VROverlay()->DestroyOverlay(handle);
                    std::cout << "[INFO] Done!" << std::endl;
                    return 0;
                };
                
                SendSock(Overlay_Calib_State);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));  // Pause for UDP delay
                Overlay_Size = 1.0;
            }
        }
    }
}