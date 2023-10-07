#include <iostream>
#include <stdio.h>
#include "openvr.h"
#include <chrono>
#include <thread>
using namespace vr;

int loop_var = 10;
int Overlay_Calib_State = 1;
float Overlay_Size = 1.0;
float Overlay_X_Pos = 0.0;
float Overlay_Y_Pos = 0.0;
float Overlay_Z_Pos = 0.0;
bool Calibrate = true;

void check_error(int line, EVRInitError error) { if (error != 0) printf("%d: error %s\n", line, VR_GetVRInitErrorAsSymbol(error)); }

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    EVRInitError error;
    VR_Init(&error, vr::VRApplication_Overlay);
    check_error(__LINE__, error);

    VROverlayHandle_t handle;
    std::cout << "Start:";

    VROverlay()->CreateOverlay("image", "image", &handle); /* key has to be unique, name doesn't matter */
    VROverlay()->SetOverlayFromFile(handle, "C:/Users/beaul/Downloads/Purple_Dot.png");
    VROverlay()->SetOverlayWidthInMeters(handle, 2);
    VROverlay()->ShowOverlay(handle);
    TrackedDevicePose_t trackedDevicePose[1];

    while (true) {

        // Print the HMD's position and the image's position
        while (Calibrate) {
            while (Overlay_Size > 0.01) {
                Overlay_Size -= 0.01;
                VROverlay()->SetOverlayWidthInMeters(handle, Overlay_Size);

                vr::HmdMatrix34_t transform = {
                    1.0f, 0.0f, 0.0f, Overlay_X_Pos,
                    0.0f, 1.0f, 0.0f, Overlay_Y_Pos,
                    0.0f, 0.0f, 1.0f, -2.0f
                };

                VROverlay()->SetOverlayTransformTrackedDeviceRelative(handle, vr::k_unTrackedDeviceIndex_Hmd, &transform);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Pauses for 10 milliseconds
            }
            if (Overlay_Size <= 0.01) {
                std::cout << Overlay_Calib_State;
                Overlay_Calib_State++;
                Overlay_Size = 1.0;
            }
            if (Overlay_Calib_State == 2) { //up left corner
                Overlay_X_Pos = 0.8;
                Overlay_Y_Pos = 0.8;
            }
            if (Overlay_Calib_State == 3) { //up middle
                Overlay_X_Pos = 0.0;
                Overlay_Y_Pos = 0.8;
            }
            if (Overlay_Calib_State == 4) { // up right corner
                Overlay_X_Pos = -0.8;
                Overlay_Y_Pos = 0.8;
            }
            if (Overlay_Calib_State == 5) { //middle left corner
                Overlay_X_Pos = -0.8;
                Overlay_Y_Pos = 0.0;
            }
            if (Overlay_Calib_State == 6) { //middle right corner
                Overlay_X_Pos = 0.8;
                Overlay_Y_Pos = 0.0;
            }
            if (Overlay_Calib_State == 7) { //bottom left corner
                Overlay_X_Pos = 0.8;
                Overlay_Y_Pos = -0.8;
            }
            if (Overlay_Calib_State == 8) { //bottom middle corner
                Overlay_X_Pos = 0.0;
                Overlay_Y_Pos = -0.8;
            }
            if (Overlay_Calib_State == 9) { //bottom right corner
                Overlay_X_Pos = -0.8;
                Overlay_Y_Pos = -0.8;
            }
            if (Overlay_Calib_State > 9) { //bottom right corner
                Calibrate = false;
                VROverlay()->DestroyOverlay(handle);
            }
        }
    }
}
