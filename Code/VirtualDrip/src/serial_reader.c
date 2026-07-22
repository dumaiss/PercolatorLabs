#define _DEFAULT_SOURCE

#include "serial_reader.h"

#include "packet_parser.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SERIAL_POLL_TIMEOUT_MS 100
#define SERIAL_REOPEN_DELAY_US 250000

/*
 * Serial input is just another byte source for PacketParser. This thread owns
 * blocking/polling behavior for the fd; decoded packet policy stays with the
 * callback supplied by main.
 */

struct SerialReader {
    pthread_t thread;
    SerialReaderConfig config;
    bool started;
};

static bool reader_should_stop(const SerialReader *reader)
{
    if (reader->config.should_stop == NULL) {
        return false;
    }

    return reader->config.should_stop(reader->config.should_stop_userdata);
}

static void reader_report_connection(
    SerialReader *reader, bool connected, bool reconnected)
{
    if (reader->config.connection_changed != NULL) {
        reader->config.connection_changed(
            connected, reconnected, reader->config.connection_userdata);
    }
}

static void reader_disconnect(SerialReader *reader, int fd, const char *reason)
{
    if (!serial_port_mark_disconnected(reader->config.port, fd)) {
        return;
    }
    fprintf(stderr, "Serial device disconnected (%s): %s\n",
        reason, serial_port_path(reader->config.port));
    reader_report_connection(reader, false, false);
}

static void *serial_reader_thread(void *context)
{
    SerialReader *reader = (SerialReader *)context;
    PacketParser parser;
    packet_parser_init(&parser, reader->config.handler, reader->config.handler_userdata);
    if (serial_port_is_connected(reader->config.port)) {
        printf("Serial packet input listening on %s at %d baud\n",
            serial_port_path(reader->config.port),
            serial_port_baud_rate(reader->config.port));
        reader_report_connection(reader, true, false);
    }

    while (!reader_should_stop(reader)) {
        int fd = serial_port_fd(reader->config.port);
        if (fd < 0) {
            if (!serial_port_reopen(reader->config.port)) {
                usleep(SERIAL_REOPEN_DELAY_US);
                continue;
            }
            packet_parser_init(
                &parser, reader->config.handler, reader->config.handler_userdata);
            fprintf(stderr, "Serial device reconnected: %s at %d baud\n",
                serial_port_path(reader->config.port),
                serial_port_baud_rate(reader->config.port));
            reader_report_connection(reader, true, true);
            continue;
        }

        struct pollfd pollfd = {
            .fd = fd,
            .events = POLLIN,
            .revents = 0,
        };
        int poll_status = poll(&pollfd, 1, SERIAL_POLL_TIMEOUT_MS);
        if (poll_status < 0) {
            if (errno == EINTR) {
                continue;
            }
            reader_disconnect(reader, fd, "poll failed");
            continue;
        }
        if (poll_status == 0) {
            continue;
        }

        uint8_t buffer[256];
        if (pollfd.revents & POLLIN) {
            ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                /* Packet callbacks run on this thread. */
                for (ssize_t index = 0; index < bytes_read; ++index) {
                    packet_parser_feed(&parser, buffer[index]);
                }
            } else if (bytes_read < 0
                       && errno != EAGAIN
                       && errno != EWOULDBLOCK
                       && errno != EINTR) {
                reader_disconnect(reader, fd, "read failed");
                continue;
            }
        }
        if (pollfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            reader_disconnect(reader, fd, "hangup/error");
        }
    }

    printf("Serial packet input stopped after %zu packet%s",
        packet_parser_packet_count(&parser),
        packet_parser_packet_count(&parser) == 1 ? "" : "s");
    printf("\n");

    return NULL;
}

SerialReader *serial_reader_start(const SerialReaderConfig *config)
{
    SerialReader *reader = calloc(1, sizeof(*reader));
    if (reader == NULL) {
        fprintf(stderr, "Failed to allocate serial reader\n");
        return NULL;
    }

    reader->config = *config;
    if (pthread_create(&reader->thread, NULL, serial_reader_thread, reader) != 0) {
        perror("pthread_create");
        free(reader);
        return NULL;
    }

    reader->started = true;
    return reader;
}

void serial_reader_join(SerialReader *reader)
{
    if (reader == NULL) {
        return;
    }

    if (reader->started) {
        pthread_join(reader->thread, NULL);
        reader->started = false;
    }
    free(reader);
}
