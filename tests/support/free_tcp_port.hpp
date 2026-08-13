#pragma once

#include <cstdint>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

inline std::uint16_t opc_free_tcp_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return static_cast<std::uint16_t>(20000 + (::getpid() % 20000));
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return static_cast<std::uint16_t>(20000 + (::getpid() % 20000));
    }
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::close(fd);
        return static_cast<std::uint16_t>(20000 + (::getpid() % 20000));
    }
    const auto port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}
