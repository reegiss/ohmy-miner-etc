#include "ohmy/stratum_client.hpp"
#include "ohmy/logger.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <stdexcept>

namespace ohmy {
namespace network {

/**
 * @brief TCP socket connection wrapper
 */
class TcpConnection {
public:
    TcpConnection() : socket_(-1), connected_(false) {}
    
    ~TcpConnection() {
        disconnect();
    }
    
    bool connect(const std::string& host, uint16_t port) {
        // Resolve hostname
        struct hostent* server = gethostbyname(host.c_str());
        if (server == nullptr) {
            LOG_ERROR("Failed to resolve host: " + host);
            return false;
        }
        
        // Create socket
        socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_ < 0) {
            LOG_ERROR("Failed to create socket");
            return false;
        }
        
        // Setup server address
        struct sockaddr_in serverAddr;
        std::memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        std::memcpy(&serverAddr.sin_addr.s_addr, server->h_addr, server->h_length);
        
        // Connect
        if (::connect(socket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            LOG_ERROR("Failed to connect to " + host + ":" + std::to_string(port));
            close(socket_);
            socket_ = -1;
            return false;
        }
        
        connected_ = true;
        LOG_INFO("Connected to " + host + ":" + std::to_string(port));
        return true;
    }
    
    void disconnect() {
        if (socket_ >= 0) {
            close(socket_);
            socket_ = -1;
            connected_ = false;
            LOG_INFO("Disconnected from pool");
        }
    }
    
    bool send(const std::string& data) {
        if (!connected_) {
            return false;
        }
        
        ssize_t sent = ::send(socket_, data.c_str(), data.length(), 0);
        if (sent < 0) {
            LOG_ERROR("Failed to send data");
            return false;
        }
        
        return true;
    }
    
    std::string receive(size_t maxSize = 4096) {
        if (!connected_) {
            return "";
        }
        
        char buffer[4096];
        ssize_t received = ::recv(socket_, buffer, std::min(maxSize, sizeof(buffer) - 1), 0);
        
        if (received < 0) {
            LOG_ERROR("Failed to receive data");
            return "";
        }
        
        if (received == 0) {
            LOG_INFO("Connection closed by server");
            disconnect();
            return "";
        }
        
        buffer[received] = '\0';
        return std::string(buffer, received);
    }
    
    bool isConnected() const {
        return connected_;
    }
    
private:
    int socket_;
    bool connected_;
};

} // namespace network
} // namespace ohmy
