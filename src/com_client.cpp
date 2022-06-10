#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "com_client.hpp"
#include <cerrno>
#include <clocale>
#include <cstring>
#include <strANSIseq.hpp>

#define LOG(...)             \
    if(m_verbose)            \
    {                        \
        printf(__VA_ARGS__); \
        fflush(stdout);      \
    }

namespace Communication
{

using namespace ESC;

Client::Client(bool verbose) : m_verbose(true)
{
    m_mutex = new std::mutex();
#ifdef WIN32
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if(err < 0)
    {
        puts("WSAStartup failed !");
        exit(EXIT_FAILURE);
    }
#endif
}

Client::~Client(void)
{
#ifdef WIN32
    WSACleanup();
#endif
    delete m_mutex;
}

int
Client::open_connection(const char *address, int port, int flags)
{
    if(port == -1)
        m_comm_mode = SERIAL_MODE;
    else
        m_comm_mode = SOCKET_MODE;

    if(m_comm_mode == SERIAL_MODE)
        this->setup_serial(address, flags);
    else if(m_comm_mode == SOCKET_MODE)
        this->setup_socket(address, port,
                           ((O_RDWR | O_NOCTTY) == flags) ? -1 : flags);

    usleep(100000);

    return m_fd;
}

int
Client::close_connection()
{
    return closesocket(m_fd);
}

int
Client::setup_socket(const char *address, int port, int timeout)
{
    SOCKADDR_IN sin = {0};
    struct hostent *hostinfo;
    TIMEVAL tv = {.tv_sec = timeout, .tv_usec = 0};
    int res;
    std::string id =
        "[" + std::string(address) + ":" + std::to_string(port) + "]";

    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(m_fd == INVALID_SOCKET)
        throw std::string("socket() invalid");

    hostinfo = gethostbyname(address);
    if(hostinfo == NULL)
        throw std::string("Unknown host") + address;

    sin.sin_addr = *(IN_ADDR *)hostinfo->h_addr;
    sin.sin_port = htons(port);
    sin.sin_family = AF_INET;

    LOG("%s\tConnection to %s in progress%s (timeout=%ds)\n",
        fstr("[TCP SOCKET]", {BOLD, FG_CYAN}).c_str(),
	id.c_str(),
	fstr("...", {BLINK_SLOW}).c_str(),
	timeout);

    if(timeout != -1)
        this->SetSocketBlockingEnabled(false); //set socket non-blocking
    res = connect(m_fd, (SOCKADDR *)&sin, sizeof(SOCKADDR)); //try to connect
    if(timeout != -1)
        this->SetSocketBlockingEnabled(true); //set socket blocking

    if(res < 0 && errno == EINPROGRESS) //if not connected instantaneously
    {
        fd_set wait_set;         //create fd set
        FD_ZERO(&wait_set);      //clear fd set
        FD_SET(m_fd, &wait_set); //add m_fd to the set
        res = select(m_fd + 1,  // return if one of the set could be wrt/rd/expt
                     NULL,      //reading set of fd to watch
                     &wait_set, //writing set of fd to watch
                     NULL,      //exepting set of fd to watch
                     &tv);      //timeout before stop watching
        if(res < 1)
            LOG("\t\tCould not connect to %s\n", fstr(id, {BOLD, FG_RED}).c_str());
        if(res == -1)
            throw id + std::string(" Error with select()");
        else if(res == 0)
            throw id + std::string(" Connection timed out");
        res = 0;
    }
    if(res == 0)
    {
        int opt; // check for errors in socket layer
        socklen_t len = sizeof(opt);
        if(getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &opt, &len) < 0)
            throw id + std::string(" Error retrieving socket options");
        if(opt) // there was an error
            throw id + " " + std::string(std::strerror(opt));
        LOG("\t\tConnected to %s\n", fstr(id, {BOLD, FG_GREEN}).c_str());
        m_is_connected = true;
    }

    return 1;
}

int
Client::setup_serial(const char *path, int flags)
{
    std::string id =
        "[" + std::string(path) + ":" + std::to_string(flags) + "]";

    LOG("\x1b[34m[TCP SOCKET]\x1b[0m\tConnection to %s in "
        "progress\x1b[5m...\x1b[0m\n",
        id.c_str());
    m_fd = open(path, flags);
    std::cout << "> Check connection: " << m_fd << std::flush;
    if(m_fd < 0)
        throw id + "[ERROR] Could not open the serial port.";

    std::cout << "OK\n" << std::flush;

    struct termios tty;
    if(tcgetattr(m_fd, &tty) != 0)
        throw id + "[ERROR] Could not get the serial port settings.";

    tty.c_cflag &= ~PARENB;  // Clear parity bit, disabling parity (most common)
    tty.c_cflag &= ~CSTOPB;  // Clear stop field, only 1 stop bit (most common)
    tty.c_cflag &= ~CSIZE;   // Clear all bits that set the data size
    tty.c_cflag |= CS8;      // 8 bits per byte (most common)
    tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow ctrl (most common)
    tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrlline(CLOCAL = 1)
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;   // Disable echo
    tty.c_lflag &= ~ECHOE;  // Disable erasure
    tty.c_lflag &= ~ECHONL; // Disable new-line echo
    tty.c_lflag &= ~ISIG;   // Disable interpretation of INTR, QUIT and SUSP
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                     ICRNL); // Disable any special handling of received bytes

    tty.c_oflag &= ~OPOST; // Prevent spe. interp. of out bytes (newline chars)
    tty.c_oflag &= ~ONLCR; // Prevent conv of newline to car. ret/line feed
    // tty.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT ON LINUX)
    // tty.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT ON LINUX)

    tty.c_cc[VTIME] = 4; // Wait for up to 1s, ret when any data is received.
    tty.c_cc[VMIN] = 0;

    // Set in/out baud rate to be 9600
    cfsetispeed(&tty, B500000);
    cfsetospeed(&tty, B500000);

    // Save tty settings, also checking for error
    if(tcsetattr(m_fd, TCSANOW, &tty) != 0)
        throw id + "[ERROR] Could not set the serial port settings.";
    return m_fd;
}

int
Client::readS(uint8_t *buffer, size_t size)
{
    std::lock_guard<std::mutex> lck(*m_mutex); //ensure only one thread using it
    int n = 0;
    if(m_comm_mode == SOCKET_MODE)
        n = recv(m_fd, buffer, size, 0);
    else if(m_comm_mode == SERIAL_MODE)
        n = read(m_fd, buffer, size);
    if(n < size)
        throw "reading error: " + std::to_string(n) + "/" +
            std::to_string(size);

    return n;
}

int
Client::writeS(const void *buffer, size_t size)
{
    std::lock_guard<std::mutex> lck(*m_mutex); //ensure only one thread using it
    int n = 0;
    if(m_comm_mode == SOCKET_MODE)
        n = send(m_fd, buffer, size, 0);
    else if(m_comm_mode == SERIAL_MODE)
        n = write(m_fd, buffer, size);
    if(n < size)
        throw "Writing error: " + std::to_string(n) + "/" +
            std::to_string(size);

    return n;
}

bool
Client::SetSocketBlockingEnabled(bool blocking)
{
    std::lock_guard<std::mutex> lck(*m_mutex); //ensure only one thread using it
    if(m_fd < 0)
        return false;

#ifdef _WIN32
    unsigned long mode = blocking ? 0 : 1;
    return (ioctlsocket(m_fd, FIONBIO, &mode) == 0) ? true : false;
#else
    int flags = fcntl(m_fd, F_GETFL, 0);
    if(flags == -1)
        return false;
    flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return (fcntl(m_fd, F_SETFL, flags) == 0) ? true : false;
#endif
}

} // namespace Communication
