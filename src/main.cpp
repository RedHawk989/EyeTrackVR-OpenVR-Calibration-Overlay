#include <iostream>
#include <stdio.h>
#include "openvr.h"
#include <chrono>
#include <thread>
#include <cmath>
#include <filesystem>
#include <zmq.hpp> // cppzmq header
#include <string>

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

    const std::string endpoint = "tcp://127.0.0.1:5555"; // Changed to localhost for connecting

    // Initialize the 0MQ context
    zmq::context_t context(1); // 1 I/O thread is common for basic usage

    // Generate a REQ socket (requester)
    zmq::socket_t socket(context, zmq::socket_type::req);

    // Connect the socket
    std::cout << "Connecting to " << endpoint << std::endl;
    socket.connect(endpoint);
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
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Pause for 1 milliseconds
        }

        if (Center_Only == true) {
            // Send the calibration state
            std::string message_content = std::to_string(Overlay_Calib_State);
            zmq::message_t message(message_content.data(), message_content.size());
            socket.send(message, zmq::send_flags::none);

            // REQ sockets *must* receive a reply after sending
            zmq::message_t reply;
            socket.recv(reply, zmq::recv_flags::none); // BLOCKING: waits for reply
            std::string reply_content = reply.to_string();
            std::cout << "[INFO] Received ACK for Center Only: " << reply_content << std::endl;

            std::cout << "[INFO] Done!" << std::endl;
            return 0;
        }

        if (Overlay_Size <= 0.03) {
            std::cout << "[INFO] Calibrated point: ";
            std::cout << Overlay_Calib_State + 1 << std::endl;

            // Send the calibration state
            std::string message_content = std::to_string(Overlay_Calib_State);
            zmq::message_t message(message_content.data(), message_content.size());
            socket.send(message, zmq::send_flags::none);

            // REQ sockets *must* receive a reply after sending
            zmq::message_t reply;
            socket.recv(reply, zmq::recv_flags::none); // BLOCKING: waits for reply
            std::string reply_content = reply.to_string();
            std::cout << "[INFO] Received ACK for point " << Overlay_Calib_State + 1 << ": " << reply_content << std::endl;

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

    VROverlay()->DestroyOverlay(handle);
    std::cout << "[INFO] Done!" << std::endl;
    VR_Shutdown();
    return 0;
}