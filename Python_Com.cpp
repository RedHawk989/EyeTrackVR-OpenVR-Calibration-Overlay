#include <iostream>
#include <ws2tcpip.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

using std::cerr;
using std::endl;

int sock() {
    SOCKET network_socket;

    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        // Handle error
    }
    network_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9002);
    inet_pton(AF_INET, "127.0.0.1", &(server_address.sin_addr));

    int connection_status = connect(network_socket, (struct sockaddr*)&server_address, sizeof(server_address));
    if (connection_status == -1) {
        cerr << "We failed to connect to the server" << endl;
    }

    closesocket(network_socket);
    WSACleanup();

    return 0;
}
