#include <boost/asio.hpp>
#include <winsock2.h>

void SendSock(int message) {
    // Create a Boost.Asio I/O context
    boost::asio::io_service io_service;

    // Create a UDP socket
    boost::asio::ip::udp::socket socket(io_service);

    // Specify the destination address and port
    boost::asio::ip::udp::endpoint remote_endpoint(
        boost::asio::ip::address::from_string("127.0.0.1"),  // IP address
        12345                                                // Port number
    );

    // Convert the integer to network byte order
    int net_message = htonl(message);

    // Send the integer to the specified UDP port
    socket.open(boost::asio::ip::udp::v4());
    socket.send_to(boost::asio::buffer(&net_message, sizeof(net_message)), remote_endpoint);

    // Close the socket
    socket.close();
}
