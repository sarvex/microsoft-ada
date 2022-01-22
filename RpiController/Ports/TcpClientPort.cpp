// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <stdio.h>
#include <string.h>
#include <stdexcept>

#include "Utils.h"
#include "TcpClientPort.h"
#include "SocketInit.h"

#ifdef _WIN32
// windows
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")

typedef int socklen_t;
static bool socket_initialized_ = false;
#else

// posix
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cerrno>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
typedef int SOCKET;
const int INVALID_SOCKET = -1;
const int ERROR_ACCESS_DENIED = EACCES;

inline int WSAGetLastError() {
	return errno;
}
const int SOCKET_ERROR = -1;
#define E_NOT_SUFFICIENT_BUFFER ENOMEM

#endif

class TcpClientPort::TcpSocketImpl
{
	SocketInit init;
	SOCKET sock = INVALID_SOCKET;
    sockaddr_in localaddr{};
    sockaddr_in remoteaddr{};
	bool closed_ = true;
public:

	bool isClosed() {
		return closed_;
	}

	static void resolveAddress(const std::string& ipAddress, int port, sockaddr_in& addr)
	{
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		addr.sin_family = AF_INET;
		addr.sin_port = htons((u_short)port);

		bool found = false;
		struct addrinfo *result = NULL;
		std::string serviceName = std::to_string(port);
		int rc = getaddrinfo(ipAddress.c_str(), serviceName.c_str(), &hints, &result);
		if (rc != 0) {
			throw std::runtime_error(Utils::stringf("TcpClientPort getaddrinfo failed with error: %d\n", rc));
		}
		for (struct addrinfo *ptr = result; ptr != NULL; ptr = ptr->ai_next)
		{
			if (ptr->ai_family == AF_INET && ptr->ai_socktype == SOCK_STREAM && ptr->ai_protocol == IPPROTO_TCP)
			{
				// found it!
				sockaddr_in* sptr = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);
				addr.sin_family = sptr->sin_family;
				addr.sin_addr.s_addr = sptr->sin_addr.s_addr;
				addr.sin_port = sptr->sin_port;
				found = true;
				break;
			}
		}

		freeaddrinfo(result);
		if (!found) {
			throw std::runtime_error(Utils::stringf("TcpClientPort could not resolve ip address for '%s:%d'\n", ipAddress.c_str(), port));
		}
	}

	int connect(const std::string& localHost, int localPort, const std::string& remoteHost, int remotePort)
	{
		sock = socket(AF_INET, SOCK_STREAM, 0);

		resolveAddress(localHost, localPort, localaddr);
		resolveAddress(remoteHost, remotePort, remoteaddr);

		// bind socket to local address.
		socklen_t addrlen = sizeof(sockaddr_in);
		int rc = bind(sock, reinterpret_cast<sockaddr*>(&localaddr), addrlen);
		if (rc < 0)
		{
			int hr = WSAGetLastError();
			throw std::runtime_error(Utils::stringf("TcpClientPort socket bind failed with error: %d\n", hr));
		}

		rc = ::connect(sock, reinterpret_cast<sockaddr*>(&remoteaddr), addrlen);
		if (rc != 0) {
			int hr = WSAGetLastError();
			throw std::runtime_error(Utils::stringf("TcpClientPort socket connect failed with error: %d\n", hr));
		}

		closed_ = false;
		return 0;
	}

    int connect(const std::string& remoteHost, int remotePort)
    {
        sock = socket(AF_INET, SOCK_STREAM, 0);

        resolveAddress(remoteHost, remotePort, remoteaddr);

        // bind socket to local address.
        socklen_t addrlen = sizeof(sockaddr_in);

        int rc = ::connect(sock, reinterpret_cast<sockaddr*>(&remoteaddr), addrlen);
        if (rc != 0) {
            int hr = WSAGetLastError();
            throw std::runtime_error(Utils::stringf("TcpClientPort socket connect failed with error: %d\n", hr));
        }

        ::getsockname(sock, reinterpret_cast<sockaddr*>(&localaddr), &addrlen);

        closed_ = false;
        return 0;
    }

	void accept(const std::string& localHost, int localPort)
	{
		SOCKET local = socket(AF_INET, SOCK_STREAM, 0);

		resolveAddress(localHost, localPort, localaddr);

		// bind socket to local address.
		socklen_t addrlen = sizeof(sockaddr_in);
		int rc = ::bind(local, reinterpret_cast<sockaddr*>(&localaddr), addrlen);
		if (rc < 0)
		{
			int hr = WSAGetLastError();
			throw std::runtime_error(Utils::stringf("TcpClientPort socket bind failed with error: %d\n", hr));
		}

		// start listening for incoming connection
		rc = ::listen(local, 1);
		if (rc < 0)
		{
			int hr = WSAGetLastError();
			throw std::runtime_error(Utils::stringf("TcpClientPort socket listen failed with error: %d\n", hr));
		}

		// accept 1
		sock = ::accept(local, reinterpret_cast<sockaddr*>(&remoteaddr), &addrlen);
		if (sock == INVALID_SOCKET) {
			int hr = WSAGetLastError();
			throw std::runtime_error(Utils::stringf("TcpClientPort accept failed with error: %d\n", hr));
		}

		closed_ = false;
	}

	// write to the serial port
	int write(const uint8_t* ptr, int count)
	{
		int hr = send(sock, reinterpret_cast<const char*>(ptr), count, 0);
		if (hr == SOCKET_ERROR)
		{
			throw std::runtime_error(Utils::stringf("TcpClientPort socket send failed with error: %d\n", hr));
		}

		return hr;
	}

	bool available()
	{
        struct timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000; // 1 millisecond
        fd_set fdset{};
        FD_SET(sock, &fdset);
		int rc = select(1, &fdset, nullptr, nullptr, &timeout);
		return rc == 1;
	}

	int read(uint8_t* result, int bytesToRead)
	{
		// try and receive something, up until port is closed anyway.

		while (!closed_)
		{
			int rc = recv(sock, reinterpret_cast<char*>(result), bytesToRead, 0);
			if (rc < 0)
			{
#ifdef _WIN32
				int hr = WSAGetLastError();
				if (hr == WSAEMSGSIZE)
				{
					// message was too large for the buffer, no problem, return what we have.					
				}
				else if (hr == WSAECONNRESET || hr == ERROR_IO_PENDING)
				{
					// try again - this can happen if server recreates the socket on their side.
					return -1;
				}
				else
#else
				int hr = errno;
				if (hr == EINTR)
				{
					// skip this, it is was interrupted.
					continue;
				}
				else
#endif
				{
					return -1;
				}
			}

			if (rc == 0)
			{
				//printf("Connection closed\n");
				return -1;
			}
			else
			{
				return rc;
			}
		}
		return -1;
	}


	void close()
	{
		if (!closed_) {
			closed_ = true;

#ifdef _WIN32
			closesocket(sock);
#else
			int fd = static_cast<int>(sock);
			::close(fd);
#endif
		}
	}

	std::string remoteAddress() {
		return inet_ntoa(remoteaddr.sin_addr);
	}

	int remotePort() {
		return ntohs(remoteaddr.sin_port);
	}

    std::string localAddress()
    {
        return inet_ntoa(localaddr.sin_addr);
    }

    int localPort()
    {
        return ntohs(localaddr.sin_port);
    }

};

//-----------------------------------------------------------------------------------------

TcpClientPort::TcpClientPort()
{
	impl_.reset(new TcpSocketImpl());
}

TcpClientPort::~TcpClientPort()
{
	close();
}

void TcpClientPort::close()
{
	impl_->close();
}

void TcpClientPort::connect(const std::string& localHost, int localPort, const std::string& remoteHost, int remotePort)
{
	impl_->connect(localHost, localPort, remoteHost, remotePort);
}

void TcpClientPort::connect(const std::string& remoteHost, int remotePort)
{
    impl_->connect(remoteHost, remotePort);
}

void TcpClientPort::accept(const std::string& localHost, int localPort)
{
	impl_->accept(localHost, localPort);
}

int TcpClientPort::write(const uint8_t* ptr, int count)
{
	return impl_->write(ptr, count);
}

bool TcpClientPort::available()
{
	return impl_->available();
}

int
TcpClientPort::read(uint8_t* buffer, int bytesToRead)
{
	return impl_->read(buffer, bytesToRead);
}

bool TcpClientPort::isClosed()
{
	return impl_->isClosed();
}

std::string TcpClientPort::remoteAddress()
{
	return impl_->remoteAddress();
}

int TcpClientPort::remotePort()
{
	return impl_->remotePort();
}

std::string TcpClientPort::localAddress()
{
    return impl_->localAddress();
}

int TcpClientPort::localPort()
{
    return impl_->localPort();
}

std::string TcpClientPort::getHostName()
{
    SocketInit init;
    char hostname[1024];
    int rc = gethostname(hostname, 1024);
    if (rc == 0) {
        return std::string(hostname);
    }
    else {
        throw std::runtime_error("gethostname failed");
    }
}

std::string TcpClientPort::getHostByName(const std::string& name)
{
    SocketInit init;
    auto entry = gethostbyname(name.c_str());
    if (entry == nullptr)
    {
        throw std::runtime_error("gethostbyname " + name + " failed");
    }
    else if (entry->h_length > 0)
    {
        uint8_t* ptr = (uint8_t*)(entry->h_addr_list[0]);
        if (entry->h_addrtype == AF_INET) 
        {
            return Utils::stringf("%d.%d.%d.%d", (int)ptr[0], (int)ptr[1], (int)ptr[2], (int)ptr[3]);
        }
        else 
        {
            // didn't want this...
        }
    }
    return "";
}

