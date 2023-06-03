#ifndef COM_CLIENT_HPP
#define COM_CLIENT_HPP
#include <iostream>
#include <math.h>
#include <mutex>
#include <stdexcept>

#ifdef WIN32 //////////// IF WINDOWS OS //////////
#include <winsock2.h>
#elif defined(linux) ///// IF LINUX OS //////////
#include <arpa/inet.h>
#include <netdb.h> // gethostbyname
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
// Linux headers
#include <errno.h> // Error integer and strerror() function
#include <fcntl.h> // Contains file controls like O_RDWR
#include <sys/time.h>
#include <termios.h> // Contains POSIX terminal control definitions
#include <unistd.h>  // write(), read(), close()
#else
#error not defined for this platform
#endif

#include <strANSIseq.hpp>

#define CRLF "\r\n"

namespace Communication
{

#if defined(linux)
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(s) close(s)
typedef int SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr SOCKADDR;
typedef struct in_addr IN_ADDR;
typedef timeval TIMEVAL;
#endif

/**
 * @brief Communication client class
 *
 * This class is used to unify the different type of communication interface (serial, IP socket, ...) and to provide a common interface to the user.
 * It is also aming to be cross-platform.
 *
 * @author Alexis Devillard
 * @date 2022
 */
class Client : virtual public ESC::CLI
{
    public:
    enum Mode
    {
        SERIAL,
        TCP,
        UDP
    };

    Client(int verbose=-1);
    ~Client();

    /**
     * @brief open_connection Open the connection the serial or network or interface
     * @param address Path or IP address
     * @param port -1 to open a serial com, else the value of the port to listen/write
     * @param flags Some flags
     * @return
     */
    int
    open_connection(Mode mode,
                    const char *address,
                    int port = -1,
                    int flags = O_RDWR | O_NOCTTY, int baud = 9600);

    void
    from_socket(SOCKET s);
    int
    close_connection();

    /**
     * @brief readS read the com inmterface.
     * @param buffer Data store in buffer.
     * @param size Nb of bytes to read.
     * @param has_crc If true the two last bytes are checked as a CRC16.
     * @param read until loop until "size" bytes have been read.
     * @return number of bytes read.
     */
    int
    readS(uint8_t *buffer, size_t size, bool has_crc = false, bool read_until=true);

    /**
     * @brief writeS write the com interface.
     * @param buffer Data to write.
     * @param size Nb of bytes to write.
     * @param add_crc If true two more bytes are added to the buffer to store a CRC16. Be sure to have enough space in the buffer.
     * @return number of bytes written.
     */
    int
    writeS(const void *buffer, size_t size, bool add_crc = false);

    /**
     * @brief Check if the connection is open.
     * @return True if success, false otherwise.
     */
    bool
    is_connected()
    {
        return m_is_connected;
    };

    /**
     * @brief CRC Compute and return the CRC over the n first bytes of buf
     * @param buf path of the dir to look inside
     * @param n reference to the vector of string that will be filled with the directories that are in the path.
     */
    uint16_t
    CRC(uint8_t *buf, int n);

    void
    get_stat(char c = 'd', int pkgSize = 6)
    {
        uint8_t buf[pkgSize];
        buf[0] = c;
        this->writeS(buf, pkgSize);
        float vals[4];
        this->readS((uint8_t *)vals, 16);

        std::cout << "mean: " << vals[0] << std::endl;
        std::cout << "std: " << sqrt(vals[1] - vals[0] * vals[0]) << std::endl;
        std::cout << "n: " << vals[2] << std::endl;
        std::cout << "max: " << vals[3] << std::endl;
    }

    private:
    /**
     * @brief crchware Generate the values for the CRC lookup table.
     * @param data The short data to generate the CRC.
     * @param genpoly The generator polynomial.
     * @param accum The accumulator value.
     * @return
     */
    uint16_t
    crchware(uint16_t data, uint16_t genpoly, uint16_t accum);

    /**
     * @brief mk_crctable Create a CRC lookup table to compute CRC16 fatser.
     * @param poly Represent the coeficients use for the polynome of the CRC
     */
    void
    mk_crctable(uint16_t poly = 0x1021);

    /**
     * @brief CRC_check Function use in CRC computation
     * @param data The data to compute.
     */
    void
    CRC_check(uint8_t data);

    /**
     * @brief setup_serial Set up the serial object.
     * @param address path of the serial port.
     * @param flags connection flags.
     * @return int return the file descriptor of the serial port.
     */
    int
    setup_serial(const char *address, int flags, int baud);

    /**
     * @brief setup_socket Set up the socket object.
     * @param address IP address of the server.
     * @param port Port of the server.
     * @return int return the file descriptor of the socket.
     */
    int
    setup_TCP_socket(const char *address, int port, int timeout = 2);

    /**
     * @brief setup_socket Set up the socket object.
     * @param address IP address of the server.
     * @param port Port of the server.
     * @return int return the file descriptor of the socket.
     */
    int
    setup_UDP_socket(const char *address, int port, int timeout = 2);

    /** Returns true on success, or false if there was an error */
    bool
    SetSocketBlockingEnabled(bool blocking);

    SOCKET m_fd;
    Mode m_comm_mode;
    bool m_is_connected = false;
    std::mutex *m_mutex;
    uint16_t m_crctable[256];
    uint16_t m_crc_accumulator;
    SOCKADDR_IN m_addr_to = {0};
    socklen_t m_size_addr;
    std::string m_id;
};

class Server : virtual public ESC::CLI
{
    public:
  Server(int verbose = -1) : ESC::CLI(verbose - 1, "Client"), m_client(verbose)
    {
        SOCKADDR_IN sin = {0};
        SOCKADDR_IN csin = {0};
        SOCKET csock;
        int sinsize = sizeof csin;
        m_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(m_fd == INVALID_SOCKET)
            throw log_error("socket() invalid");

        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        sin.sin_port = htons(5001);
        sin.sin_family = AF_INET;

	int yes=1;
	if (setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
    perror("setsockopt");
    exit(1);
}
	
        if(bind(m_fd, (SOCKADDR *)&sin, sizeof sin) == SOCKET_ERROR)
            throw log_error("bind()");
        if(listen(m_fd, 5) == SOCKET_ERROR)
            throw log_error("listen()");
        csock = accept(m_fd, (SOCKADDR *)&csin, (socklen_t *)&sinsize);
        if(csock == INVALID_SOCKET)
            throw log_error("accept()");

        m_client.from_socket(csock);
    };
  ~Server()
  {
    close(m_fd);
    m_client.close_connection();
    std::cout << "closing socket" << std::endl;
  }
public:
    SOCKET m_fd;
    Client m_client;
};

} // namespace Communication

#endif //COM_CLIENT_HPP
