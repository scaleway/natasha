#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "natasha.h"


struct client {
    int fd;
    char buf[4096];
};


static int
command_help(struct client *client)
{
    dprintf(client->fd,
            "Available commands:\n\n"
            "  exit, quit: close the connection\n"
            "        help: show this message\n"
            "      reload: reload configuration\n"
            "       stats: print ports and queues statistics\n");
    return 0;
}

static int
command_quit(struct client *client)
{
    return -1;
}

static int
run_command(struct client *client)
{
    struct {
        char *command;
        int (*func)(struct client *);
    } commands[] = {
        {"quit", command_quit},
        {"exit", command_quit},
        {"help", command_help},
    };
    size_t i;

    for (i = 0; i < sizeof(commands) / sizeof(*commands); ++i) {
        if (strcmp(commands[i].command, client->buf) == 0) {
            return commands[i].func(client);
        }
    }
    dprintf(client->fd, "%s: command not found\n", client->buf);
    return 0;
}

static int
run_commands(struct client *client)
{
    char *end;
    size_t rest;

    while ((end = strchr(client->buf, '\n'))) {
        *end = '\0';
        if (run_command(client) < 0) {
            return -1;
        }
        rest = end - client->buf + 1;
        memcpy(client->buf, &client->buf[rest], sizeof(client->buf) - rest);
    }
    return 0;
}

static void
disconnect_client(struct client *client)
{
    close(client->fd);
    memset(client, 0, sizeof(*client));
}

/*
 * Accept connections and answer to queries.
 */
static int
adm_loop(int s)
{
    struct client clients[2];
    const int max_clients = sizeof(clients) / sizeof(*clients);
    int num_clients;

    memset(clients, 0, sizeof(clients));
    num_clients = 0;

    while (1) {
        size_t i;
        fd_set readfds;
        int maxfd;
        int events;

        FD_ZERO(&readfds);
        FD_SET(s, &readfds);

        // Setup maxfd and readfds
        maxfd = s;
        for (i = 0; i < max_clients; ++i) {
            if (clients[i].fd) {
                FD_SET(clients[i].fd, &readfds);
                if (clients[i].fd >= maxfd) {
                    maxfd = clients[i].fd;
                }
            }
        }

        events = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (events < 0) {
            RTE_LOG(ERR, APP,
                    "Adm server: cannot select on adm UNIX socket: %s\n",
                    strerror(errno));
            return EXIT_FAILURE;
        }

        // New client?
        if (FD_ISSET(s, &readfds)) {
            struct sockaddr_un client;
            int cs;
            socklen_t len;

            // Accept
            if ((cs = accept(s, (struct sockaddr *)&client, &len)) < 0) {
                RTE_LOG(ERR, APP, "Adm server: accept error: %s\n",
                        strerror(errno));
            }
            // Too many connections, reject client
            if (num_clients >= max_clients) {
                RTE_LOG(ERR, APP,
                        "Adm server: reject client (too many connections)\n");
                close(cs);
            }
            // Append client
            else {
                for (i = 0; i < max_clients; ++i) {
                    if (clients[i].fd == 0) {
                        clients[i].fd = cs;
                        ++num_clients;
                        break ;
                    }
                }
            }
            --events;
        }

        // Read clients commands
        while (--events >= 0) {
            for (i = 0; i < max_clients; ++i) {
                if (FD_ISSET(clients[i].fd, &readfds)) {
                    size_t curlen = strlen(clients[i].buf);
                    ssize_t nbread;
                    size_t to_read;

                    to_read = sizeof(clients[i].buf) - curlen - 1;
                    if (to_read == 0) {
                        RTE_LOG(ERR, APP,
                                "Adm server: command too long, close client\n");
                        disconnect_client(&clients[i]);
                        --num_clients;
                        break ;
                    }

                    nbread = read(clients[i].fd, clients[i].buf + curlen, to_read);
                    FD_CLR(clients[i].fd, &readfds);

                    // error
                    if (nbread < 0) {
                        RTE_LOG(ERR, APP, "Adm server: client read error\n");
                        disconnect_client(&clients[i]);
                        --num_clients;
                    }
                    // disconnect
                    else if (nbread == 0) {
                        disconnect_client(&clients[i]);
                        --num_clients;
                    }
                    // append and handle commands
                    else {
                        clients[i].buf[curlen + nbread] = 0;
                        if (run_commands(&clients[i]) < 0) {
                            disconnect_client(&clients[i]);
                            --num_clients;
                        }
                    }
                    break ;
                }
            }
        }
    }
    return EXIT_FAILURE;
}

/*
 * Run the administration server forerver.
 */
int
adm_server()
{
    int s;
    struct sockaddr_un local;
    char *socket_path = "/var/run/natasha.socket";
    size_t len;

    if (strlen(socket_path) >=
            sizeof(local.sun_path) / sizeof(*local.sun_path)) {
        return EXIT_FAILURE;
    }

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        RTE_LOG(ERR, APP, "Cannot create adm UNIX socket: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, socket_path);

    if (unlink(local.sun_path) < 0 && errno != ENOENT) {
        RTE_LOG(ERR, APP, "Cannot unlink existing adm UNIX socket: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    len = strlen(local.sun_path) + sizeof(local.sun_family);

    if (bind(s, (struct sockaddr *)&local, len) < 0) {
        RTE_LOG(ERR, APP, "Cannot bind adm UNIX socket: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (listen(s, 2) < 0) {
        RTE_LOG(ERR, APP, "Cannot listen on adm UNIX socket: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    return adm_loop(s);
}
