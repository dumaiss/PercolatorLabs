#define _DEFAULT_SOURCE

#include "serial_port.h"

#include "protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*
 * This module owns all POSIX serial details. The rest of the proxy deals in a
 * SerialPort handle and either framed packet bytes or default-mode raw terminal
 * input bytes; raw termios setup, kernel-managed RTS/CTS flow control, write
 * completion, and transmit locking stay here.
 */

struct SerialPort {
    /* Owned nonblocking POSIX fd opened read/write. */
    int fd;
    /* Owned copy of the path, used for diagnostics. */
    char *path;
    /* Configured baud rate accepted by baud_to_speed(). */
    int baud_rate;
    /* Protects encoded packet writes against concurrent transmit paths. */
    pthread_mutex_t tx_mutex;
};

static speed_t baud_to_speed(int baud_rate)
{
    switch (baud_rate) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    case 230400:
        return B230400;
    default:
        return 0;
    }
}

static bool configure_serial_port(int fd, int baud_rate)
{
    speed_t speed = baud_to_speed(baud_rate);
    if (speed == 0) {
        fprintf(stderr, "Unsupported baud rate %d\n", baud_rate);
        return false;
    }

    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        perror("tcgetattr");
        return false;
    }

    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    /* Raw 8N1: no echo, line editing, software flow control, or CR/LF mapping. */
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;

#ifdef CRTSCTS
    options.c_cflag |= CRTSCTS;
#else
    fprintf(stderr, "Hardware RTS/CTS flow control is not supported by this termios implementation\n");
    return false;
#endif

    options.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    options.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    options.c_oflag &= ~OPOST;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("tcsetattr");
        return false;
    }

    if (tcgetattr(fd, &options) != 0) {
        perror("tcgetattr");
        return false;
    }
#ifdef CRTSCTS
    if ((options.c_cflag & CRTSCTS) == 0) {
        fprintf(stderr, "Failed to enable hardware RTS/CTS flow control\n");
        return false;
    }
#endif

    tcflush(fd, TCIOFLUSH);
    return true;
}

static bool wait_serial_writable(int fd, int timeout_ms)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    for (;;) {
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc > 0) {
            if (pfd.revents & POLLOUT) {
                return true;
            }
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                return false;
            }
            continue;
        }

        if (rc == 0) {
            fprintf(stderr, "serial write timeout waiting for CTS/writable\n");
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        perror("poll");
        return false;
    }
}

static bool write_all(int fd, const void *buffer, size_t length)
{
    const uint8_t *cursor = (const uint8_t *)buffer;

    while (length > 0) {
        ssize_t written = write(fd, cursor, length);

        if (written > 0) {
            cursor += written;
            length -= (size_t)written;
            continue;
        }

        if (written == 0) {
            if (!wait_serial_writable(fd, 1000)) {
                return false;
            }
            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!wait_serial_writable(fd, 1000)) {
                return false;
            }
            continue;
        }

        perror("write");
        return false;
    }

    return true;
}

SerialPort *serial_port_open(const char *path, int baud_rate)
{
    if (baud_to_speed(baud_rate) == 0) {
        fprintf(stderr, "Unsupported baud rate %d\n", baud_rate);
        return NULL;
    }

    SerialPort *port = calloc(1, sizeof(*port));
    if (port == NULL) {
        fprintf(stderr, "Failed to allocate serial port\n");
        return NULL;
    }

    port->path = strdup(path);
    if (port->path == NULL) {
        fprintf(stderr, "Failed to allocate serial path\n");
        free(port);
        return NULL;
    }

    port->fd = -1;
    port->baud_rate = baud_rate;
    pthread_mutex_init(&port->tx_mutex, NULL);
    if (!serial_port_reopen(port)) {
        fprintf(stderr,
            "Serial device %s is unavailable; waiting for it to appear\n",
            path);
    }
    return port;
}

void serial_port_close(SerialPort *port)
{
    if (port == NULL) {
        return;
    }

    pthread_mutex_lock(&port->tx_mutex);
    int fd = port->fd;
    port->fd = -1;
    if (fd >= 0) {
        close(fd);
    }
    pthread_mutex_unlock(&port->tx_mutex);
    pthread_mutex_destroy(&port->tx_mutex);
    free(port->path);
    free(port);
}

int serial_port_fd(SerialPort *port)
{
    if (port == NULL) {
        return -1;
    }
    pthread_mutex_lock(&port->tx_mutex);
    int fd = port->fd;
    pthread_mutex_unlock(&port->tx_mutex);
    return fd;
}

bool serial_port_is_connected(SerialPort *port)
{
    return serial_port_fd(port) >= 0;
}

bool serial_port_mark_disconnected(SerialPort *port, int expected_fd)
{
    if (port == NULL || expected_fd < 0) {
        return false;
    }

    pthread_mutex_lock(&port->tx_mutex);
    bool changed = port->fd == expected_fd;
    if (changed) {
        port->fd = -1;
        close(expected_fd);
    }
    pthread_mutex_unlock(&port->tx_mutex);
    return changed;
}

bool serial_port_reopen(SerialPort *port)
{
    if (port == NULL) {
        return false;
    }

    int fd = open(port->path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    if (!configure_serial_port(fd, port->baud_rate)) {
        close(fd);
        return false;
    }

    pthread_mutex_lock(&port->tx_mutex);
    if (port->fd >= 0) {
        pthread_mutex_unlock(&port->tx_mutex);
        close(fd);
        return true;
    }
    port->fd = fd;
    pthread_mutex_unlock(&port->tx_mutex);
    return true;
}

const char *serial_port_path(const SerialPort *port)
{
    return port == NULL ? "" : port->path;
}

int serial_port_baud_rate(const SerialPort *port)
{
    return port == NULL ? 0 : port->baud_rate;
}

static bool build_packet_bytes(
    uint8_t type,
    const uint8_t *payload,
    uint16_t length,
    uint8_t *bytes,
    size_t *byte_count)
{
    Packet packet;
    packet.length = length;
    packet.type = type;
    if (length > 0 && payload != NULL) {
        memcpy(packet.payload, payload, length);
    }

    return packet_encode(&packet, bytes, PACKET_MAX_WIRE_SIZE, byte_count);
}

bool serial_port_send_packet(SerialPort *port, uint8_t type, const uint8_t *payload, uint16_t length)
{
    if (port == NULL) {
        return false;
    }

    uint8_t bytes[PACKET_MAX_WIRE_SIZE];
    size_t byte_count = 0;
    if (!build_packet_bytes(type, payload, length, bytes, &byte_count)) {
        return false;
    }

    /*
     * Keep each encoded packet contiguous even if future transmit paths share
     * this port with the keyboard writer thread.
     */
    pthread_mutex_lock(&port->tx_mutex);
    bool sent = port->fd >= 0 && write_all(port->fd, bytes, byte_count);
    pthread_mutex_unlock(&port->tx_mutex);

    return sent;
}

bool serial_port_send_packet_paced(
    SerialPort *port,
    uint8_t type,
    const uint8_t *payload,
    uint16_t length,
    unsigned inter_byte_delay_us)
{
    if (port == NULL) {
        return false;
    }

    uint8_t bytes[PACKET_MAX_WIRE_SIZE];
    size_t byte_count = 0;
    if (!build_packet_bytes(type, payload, length, bytes, &byte_count)) {
        return false;
    }

    pthread_mutex_lock(&port->tx_mutex);
    bool sent = port->fd >= 0;
    for (size_t index = 0; sent && index < byte_count; ++index) {
        if (!write_all(port->fd, &bytes[index], 1)) {
            sent = false;
            break;
        }
        if (inter_byte_delay_us > 0 && index + 1 < byte_count) {
            usleep(inter_byte_delay_us);
        }
    }
    pthread_mutex_unlock(&port->tx_mutex);

    return sent;
}

bool serial_port_wait_output_drained(SerialPort *port, int timeout_ms)
{
    if (port == NULL) {
        return false;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }

    pthread_mutex_lock(&port->tx_mutex);
    int fd = port->fd;
    if (fd < 0) {
        pthread_mutex_unlock(&port->tx_mutex);
        return false;
    }

    bool drained = false;

#ifdef TIOCOUTQ
    int elapsed_ms = 0;
    for (;;) {
        int pending = 0;
        if (ioctl(fd, TIOCOUTQ, &pending) == 0) {
            if (pending <= 0) {
                drained = true;
                goto done;
            }
        } else if (errno == EINTR) {
            continue;
        } else if (errno == ENOTTY || errno == EINVAL) {
            break;
        } else {
            perror("ioctl TIOCOUTQ");
            goto done;
        }

        if (elapsed_ms >= timeout_ms) {
            fprintf(stderr, "serial drain timeout with output bytes pending\n");
            goto done;
        }
        usleep(1000);
        ++elapsed_ms;
    }
#else
    (void)timeout_ms;
#endif

    if (tcdrain(fd) == 0 || errno == EINTR) {
        drained = true;
    } else {
        perror("tcdrain");
    }

done:
    pthread_mutex_unlock(&port->tx_mutex);
    return drained;
}

bool serial_port_send_raw(SerialPort *port, const uint8_t *bytes, size_t length)
{
    if (port == NULL || bytes == NULL || length == 0) {
        return false;
    }

    pthread_mutex_lock(&port->tx_mutex);
    bool sent = port->fd >= 0 && write_all(port->fd, bytes, length);
    pthread_mutex_unlock(&port->tx_mutex);

    return sent;
}
