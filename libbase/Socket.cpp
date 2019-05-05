// Socket.cpp - an IOChannel for sockets
//
//   Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012
//   Free Software Foundation, Inc
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//

#ifdef HAVE_CONFIG_H
# include "gnashconfig.h"
#endif

#include "Socket.h"

#include <cerrno>
#include <csignal>
#include <cstdint>

#include "GnashSystemNetHeaders.h"
#include "GnashSystemFDHeaders.h"
#include "log.h"
#include "utility.h"
#include "GnashAlgorithm.h"
#include "GnashSystemNetHeaders.h"

namespace gnash {

Socket::Socket()
    :
    _connected(false),
    _socket(0),
    _size(0),
    _pos(0),
    _error(false)
{ }

bool
Socket::connected() const
{
    if (_connected) {
        return true;
    }
    if (!_socket) {
        return false;
    }

    size_t retries = 10;
    fd_set fdset;
    struct timeval tval;

    while (retries-- > 0) {

        FD_ZERO(&fdset);
        FD_SET(_socket, &fdset);
            
        tval.tv_sec = 0;
        tval.tv_usec = 103;
            
        const int ret = ::select(_socket + 1, nullptr, &fdset, nullptr, &tval);
        
        // Select timeout
        if (ret == 0) continue;
           
        if (ret > 0) {
            int val = 0;
            socklen_t len = sizeof(val);
            // NB: the cast to char* is needed for windows and is harmless
            // for POSIX.
            if (::getsockopt(_socket, SOL_SOCKET, SO_ERROR,
                        reinterpret_cast<char*>(&val), &len) < 0) {
                log_debug("Socket Error");
                _error = true;
                return false;
            }

            if (!val) {
                _connected = true;
                return true;
            }
            _error = true;
            return false;
        }

        // If interrupted by a system call, try again
        if (ret == -1) {
            const int err = errno;
            if (err == EINTR) {
                log_debug("Socket interrupted by a system call");
                continue;
            }

            log_error(_("XMLSocket: The socket was never available"));
            _error = true;
            return false;
        }
    }
    return false;
} 
    

void
Socket::close()
{
    if (_socket) ::close(_socket);
    _socket = 0;
    _size = 0;
    _pos = 0;
    _connected = false;
    _error = false;
}

namespace {

addrinfo* getAddrInfo(const std::string& hostname, std::uint16_t port)
{
    addrinfo req = addrinfo(), *ans = nullptr;
    
    req.ai_family = AF_UNSPEC;  // Allow IPv4 or IPv6
    req.ai_socktype = SOCK_STREAM;

    std::string portNo = std::to_string(port);
    int code = getaddrinfo(hostname.c_str(), portNo.c_str(), &req, &ans);
    if (code != 0) {
        log_error(_("getaddrinfo() failed with code: #%d - %s"),
                 code, gai_strerror(code));
        return nullptr;
    }

    return ans;
}
  
}

bool
Socket::connect(const std::string& hostname, std::uint16_t port)
{
    // We use _socket here because connected() or _connected might not
    // be true if a connection attempt is underway but not completed.
    if (_socket) {
        log_error(_("Connection attempt while already connected"));
        return false;
    }

    // If _socket is 0, either there has been no connection, or close() has
    // been called. There must not be an error in either case.
    assert(!_error);
    
    if (hostname.empty()) {
        return false;
    }

    // This is used for ::connect()
    std::unique_ptr<addrinfo, decltype(freeaddrinfo)*> ans(getAddrInfo(hostname, port), 
        freeaddrinfo);

    if (!ans) {
        return false;
    }

    // display all the IP numbers
    if (LogFile::getDefaultInstance().getVerbosity() != 0) {
        for(const addrinfo* ot = ans.get(); ot; ot = ot->ai_next) {

            char clienthost [INET6_ADDRSTRLEN] = {};
            int code = getnameinfo(ot->ai_addr, ot->ai_addrlen,
                               clienthost, sizeof(clienthost),
                               nullptr, 0, NI_NUMERICHOST);

            if (code != 0) {
                log_error(_("getnameinfo() failed: %1%"), gai_strerror(code));
            } else {
                log_debug("%s has address of: %s", hostname, clienthost);
            }
        }
    }

    // Multiple IPV$ and IPV6 numbers may be returned, so we try them all if
    // required
    const addrinfo *it = ans.get();
    while (it) {
        _socket = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (_socket < 0) {
            const int err = errno;
            log_error(_("Socket creation failed: %s"), std::strerror(err));
            _socket = 0;
            // Try the next IP number
            it = it->ai_next;
        } else {
            break;
        }
    }

    if (!it) {
        log_error(_("Socket creation attempt(s) failed: giving up."));
        return false;
    }

#ifndef _WIN32
    // Set non-blocking.
    const int flag = ::fcntl(_socket, F_GETFL, 0);
    ::fcntl(_socket, F_SETFL, flag | O_NONBLOCK);
#endif

    // Attempt connection
    int ret = ::connect(_socket, it->ai_addr, it->ai_addrlen);
    if (ret < 0) {
        const int err = errno;
#ifndef _WIN32
        if (err != EINPROGRESS) {
            log_error(_("Failed to connect to socket: %s"), std::strerror(err));
            _socket = 0;
            return false;
        }
#else
        return false;
#endif
    }

    // Magic timeout number. Use rcfile ?
    const struct timeval tv = { 120, 0 };
    
    // NB: the cast to const char* is needed for windows and is harmless
    // for POSIX.
    if (::setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&tv), sizeof(tv))) {
        log_error(_("Setting socket timeout failed"));
    }
    
    const int on = 1;
    // NB: the cast to const char* is needed for windows and is harmless
    // for POSIX.
    ::setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&on), sizeof(on));
    
    assert(_socket);
    
    return true;
}

void
Socket::fillCache()
{
    // Read position is always _pos + _size wrapped.
    const size_t cacheSize = arraySize(_cache);
    size_t start = (_pos + _size) % cacheSize;

    char* startpos = _cache + start;

    while (1) {

        // The end pos is either the end of the cache or the first 
        // unprocessed byte.
        char* endpos = _cache + ((startpos < _cache + _pos) ?
                _pos : cacheSize);

        const int thisRead = endpos - startpos;
        assert(thisRead >= 0);

        const int bytesRead = ::recv(_socket, startpos, thisRead, 0);
        
        if (bytesRead == -1) {
            const int err = errno;
#ifndef _WIN32 
            if (err == EWOULDBLOCK || err == EAGAIN) {
                // Nothing to read. Carry on.
                return;
            }
#endif
            log_error(_("Socket receive error %s"), std::strerror(err));
            _error = true;
            return;
        }


        _size += bytesRead;

        // If there weren't enough bytes, that's it.
        if (bytesRead < thisRead) break;

        // If we wrote up to the end of the cache, try writing more to the
        // beginning.
        startpos = _cache;
    }
    
}


// Do a single read and report how many bytes were read.
std::streamsize 
Socket::read(void* dst, std::streamsize num)
{
 
    if (num < 0) return 0;

    if (_size < num && !_error) {
        fillCache();
    }

    if (_size < num) return 0;
    return readNonBlocking(dst, num);

}

std::streamsize
Socket::readNonBlocking(void* dst, std::streamsize num)
{
    if (bad()) return 0;
    
    char* ptr = static_cast<char*>(dst);
    
    if (!_size && !_error) {
        fillCache();
    }

    size_t cacheSize = arraySize(_cache);

    // First read from pos to end

    // Maximum bytes available to read.
    const size_t canRead = std::min<size_t>(_size, num);
    
    size_t toRead = canRead;

    // Space to the end (for the first read).
    const int thisRead = std::min<size_t>(canRead, cacheSize - _pos);

    std::copy(_cache + _pos, _cache + _pos + thisRead, ptr);
    _pos += thisRead;
    _size -= thisRead;
    toRead -= thisRead;

    if (toRead) {
        std::copy(_cache, _cache + toRead, ptr + thisRead);
        _pos = toRead;
        _size -= toRead;
        toRead = 0;
    }

    return canRead - toRead;
}

std::streamsize
Socket::write(const void* src, std::streamsize num)
{
    if (bad()) return 0;
    int toWrite = num;

    const char* buf = static_cast<const char*>(src);

#ifndef _WIN32
    // Prevent sigpipe (which isn't a standard C signal)
    // until leaving this function.
    const struct SignalSetter
    {
        typedef void(*SigHandler)(int);
        SignalSetter() : _h(std::signal(SIGPIPE, SIG_IGN)) {}
        ~SignalSetter() { std::signal(SIGPIPE, _h); }
    private:
        const SigHandler _h;
    } setter;
#endif

    // For broken pipe we prefer being notified with
    // a return of -1 from ::send.

    while (toWrite > 0) {
        int bytesSent = ::send(_socket, buf, toWrite, 0);
        if (bytesSent < 0) {
            const int err = errno;
            log_error(_("Socket send error %s"), std::strerror(err));
            _error = true;
            return 0;
        }

        if (!bytesSent) break;
        toWrite -= bytesSent;
        buf += bytesSent;
    }
    return num - toWrite;
}

std::streampos
Socket::tell() const
{
    log_error(_("tell() called for Socket"));
    return static_cast<std::streamsize>(-1);
}

bool
Socket::seek(std::streampos)
{
    log_error(_("seek() called for Socket"));
    return false;
}

void
Socket::go_to_end()
{
    log_error(_("go_to_end() called for Socket"));
}

bool
Socket::eof() const
{
    return !_size && bad();
}

} // namespace gnash
