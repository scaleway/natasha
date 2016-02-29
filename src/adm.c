#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "natasha.h"


struct client {
    int fd;
    char buf[4096];
};


static int
command_help(struct client *client, struct core *cores, int argc, char **argv)
{
    dprintf(client->fd,
            "Available commands:\n\n"
            "  exit, quit: close the connection\n"
            "        help: show this message\n"
            "      reload: reload configuration\n"
            "       stats: print ports and queues statistics\n"
            "       reset: reset statistics\n");
    return 0;
}

static int
command_quit(struct client *client, struct core *cores, int argc, char **argv)
{
    return -1;
}

static int
command_reload(struct client *client, struct core *cores,
               int argc, char **argv)
{
    app_config_reload_all(cores, argc, argv, client->fd);
    return 0;
}

static int
command_stats(struct client *client, struct core *cores, int argc, char **argv)
{
    stats_display(client->fd);
    return 0;
}

static int
command_reset(struct client *client, struct core *cores, int argc, char **argv)
{
    stats_reset(client->fd);
    return 0;
}

static int
run_command(struct client *client, struct core *cores, int argc, char **argv)
{
    struct {
        char *command;
        int (*func)(struct client *, struct core *, int, char **);
    } commands[] = {
        {"quit", command_quit},
        {"exit", command_quit},
        {"help", command_help},
        {"reload", command_reload},
        {"stats", command_stats},
        {"reset", command_reset},
    };
    size_t i;
    int ret;
    static size_t ncommand = 0;

    ++ncommand;
    ret = 0;

    dprintf(client->fd, "--- command %lu ---\n", ncommand);

    for (i = 0; i < sizeof(commands) / sizeof(*commands); ++i) {
        if (strcmp(commands[i].command, client->buf) == 0) {
            ret = commands[i].func(client, cores, argc, argv);
            break ;
        }
    }

    if (i == sizeof(commands) / sizeof(*commands)) {
        dprintf(client->fd, "%s: command not found\n", client->buf);
    }

    dprintf(client->fd, "--- end command %lu ---\n", ncommand);
    return ret;
}

static int
run_commands(struct client *client, struct core *cores, int argc, char **argv)
{
    char *end;
    size_t rest;

    while ((end = strchr(client->buf, '\n'))) {
        *end = '\0';
        if (run_command(client, cores, argc, argv) < 0) {
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

static void
check_slaves_alive(int *slaves_alive)
{
    int core;
    int ok;

    ok = 0;
    RTE_LCORE_FOREACH_SLAVE(core) {
        if (rte_eal_get_lcore_state(core) == RUNNING) {
            ++ok;
        }
    }

    if (ok == 0) {
        rte_exit(EXIT_FAILURE, "No slave running, exit\n");
    }

    if (ok < *slaves_alive) {
        RTE_LOG(EMERG, APP,
                "Some cores stopped working! Only %i cores are running\n", ok);
    }
    *slaves_alive = ok;
}

/*
 * Accept connections and answer to queries.
 */
static int
adm_loop(int s, struct core *cores, int argc, char **argv)
{
    struct client clients[2];
    const int max_clients = sizeof(clients) / sizeof(*clients);
    int num_clients;
    int slaves_alive;

    memset(clients, 0, sizeof(clients));
    num_clients = 0;
    slaves_alive = 0;

    while (1) {
        size_t i;
        fd_set readfds;
        int maxfd;
        int events;
        struct timeval timeout;

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

        // Setup timeout
        memset(&timeout, 0, sizeof(timeout));
        timeout.tv_sec = 1;

        events = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (events < 0) {
            if (errno != EINTR) {
                RTE_LOG(ERR, APP,
                        "Adm server: cannot select on adm socket: %s\n",
                        strerror(errno));
                return EXIT_FAILURE;
            }
        }

        // if slaves aren't alive, quit
        check_slaves_alive(&slaves_alive);

        if (events) {
            // New client?
            if (FD_ISSET(s, &readfds)) {
                struct sockaddr_un client;
                int cs;
                socklen_t len;

                // Accept
                if ((cs = accept(s, (struct sockaddr *)&client, &len)) < 0) {
                    RTE_LOG(ERR, APP, "Adm server: accept error: %s\n",
                            strerror(errno));
                } else {
                    // Too many connections, reject client
                    if (num_clients >= max_clients) {
                        RTE_LOG(
                            ERR, APP,
                            "Adm server: reject client (too many connections)\n"
                        );
                        close(cs);
                    }
                    // Set client socket non blocking
                    else if (fcntl(cs, F_SETFL, O_NONBLOCK) < 0) {
                        RTE_LOG(
                            ERR, APP,
                            "Adm server: reject client (can't ENONBLOCK)\n"
                        );
                        close(cs);
                    }
                    // Everything ok, append client
                    else {
                        for (i = 0; i < max_clients; ++i) {
                            if (clients[i].fd == 0) {
                                clients[i].fd = cs;
                                ++num_clients;
                                break ;
                            }
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

                        // error
                        if (nbread < 0) {
                            RTE_LOG(ERR, APP, "Adm server: client read error\n");
                            disconnect_client(&clients[i]);
                            --num_clients;
                        }
                        // client disconnection
                        else if (nbread == 0) {
                            disconnect_client(&clients[i]);
                            --num_clients;
                        }
                        // append and handle commands
                        else {
                            clients[i].buf[curlen + nbread] = 0;

                            if (run_commands(&clients[i],
                                             cores, argc, argv) < 0) {
                                disconnect_client(&clients[i]);
                                --num_clients;
                            }
                        }
                        break ;
                    }
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
adm_server(struct core *cores, int argc, char **argv)
{
    int s;
    struct sockaddr_in addr;
    int yes;

    // Make write() return -1 instead of raising SIGPIPE if we write to a
    // disconnected client.
    signal(SIGPIPE, SIG_IGN);

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        RTE_LOG(ERR, APP, "Cannot create adm socket: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    yes = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        RTE_LOG(ERR, APP, "Cannot setsockopt adm socket: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(4242);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        RTE_LOG(ERR, APP, "Cannot bind adm socket: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (listen(s, 2) < 0) {
        RTE_LOG(ERR, APP, "Cannot listen on adm socket: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    return adm_loop(s, cores, argc, argv);
}
