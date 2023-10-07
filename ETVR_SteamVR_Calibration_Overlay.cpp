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

void check_error(int line, EVRInitError error) { if (error != 0) printf("%d: error %s\n", line, VR_GetVRInitErrorAsSymbol(error)); }

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    EVRInitError error;
    VR_Init(&error, vr::VRApplication_Overlay);
    check_error(__LINE__, error);

    VROverlayHandle_t handle;
    std::cout << "start";

    VROverlay()->CreateOverlay("image", "image", &handle); /* key has to be unique, name doesn't matter */
    VROverlay()->SetOverlayFromFile(handle, "C:/Users/beaul/Downloads/logo_light.png");
    VROverlay()->SetOverlayWidthInMeters(handle, 2);
    VROverlay()->ShowOverlay(handle);
    TrackedDevicePose_t trackedDevicePose[1];

    while (true) {

        // Print the HMD's position and the image's position
        while (Overlay_Size > 0.01) {
            Overlay_Size -= 0.01;
            VROverlay()->SetOverlayWidthInMeters(handle, Overlay_Size);

            vr::TrackedDevicePose_t trackedDevicePose[vr::k_unMaxTrackedDeviceCount];
            vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, trackedDevicePose, vr::k_unMaxTrackedDeviceCount);

            vr::HmdMatrix34_t hmdPose = trackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;

            float hmdPos[3] = { hmdPose.m[0][3], hmdPose.m[1][3], hmdPose.m[2][3] };
            float desiredDistance = 2.0;  // Adjust this value to change the distance between the HMD and the image
            float direction[3] = { 0.0f, 0.0f, -desiredDistance };

            float rotatedDirection[3] = {
                hmdPose.m[0][0] * direction[0] + hmdPose.m[1][0] * direction[1] + hmdPose.m[2][0] * direction[2],
                hmdPose.m[0][1] * direction[0] + hmdPose.m[1][1] * direction[1] + hmdPose.m[2][1] * direction[2],
                hmdPose.m[0][2] * direction[0] + hmdPose.m[1][2] * direction[1] + hmdPose.m[2][2] * direction[2]
            };

            // Flip the left and right directions
            rotatedDirection[0] *= -1;

            float imagePos[3] = {
                hmdPos[0] + rotatedDirection[0],
                hmdPos[1] + rotatedDirection[1],
                hmdPos[2] + rotatedDirection[2]
            };

            vr::HmdMatrix34_t transform = {
                hmdPose.m[0][0], hmdPose.m[0][1], hmdPose.m[0][2], (imagePos[0] + Overlay_X_Pos),
                hmdPose.m[1][0], hmdPose.m[1][1], hmdPose.m[1][2], (imagePos[1] + Overlay_Y_Pos),
                hmdPose.m[2][0], hmdPose.m[2][1], hmdPose.m[2][2], imagePos[2]
            };

          //  std::cout << "HMD Position: (" << hmdPos[0] << ", " << hmdPos[1] << ", " << hmdPos[2] << ")" << std::endl;
          //  std::cout << "Image Position: (" << imagePos[0] << ", " << imagePos[1] << ", " << imagePos[2] << ")" << std::endl;

            VROverlay()->SetOverlayTransformAbsolute(handle, TrackingUniverseStanding, &transform);
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
    }

    while (true) {}
    return 0;
}
