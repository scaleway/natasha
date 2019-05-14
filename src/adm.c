/* vim: ts=4 sw=4 et */
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

#include <rte_ethdev.h>

#include "natasha.h"
#include "cli.h"

static int
handle_cmd_status(struct natasha_client *client, struct core *cores,
                  uint8_t cmd_type)
{
    struct natasha_cmd_reply reply;
    int nb;

    reply.type = cmd_type;
    reply.status = NATASHA_REPLY_OK;

    nb = send(client->fd, &reply, sizeof(reply), 0);
    if (nb != sizeof(reply)) {
        RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                __func__, (uint32_t)sizeof(reply), nb);
        return -1;
    }

    return 0;
}

static int
handle_cmd_reload(struct natasha_client *client, struct core *cores,
                  uint8_t cmd_type)
{
    struct natasha_cmd_reply reply;
    int nb;

    reply.type = cmd_type;
    /* Reload the configuration using the default config file path */
    reply.status = app_config_reload_all(cores, 0, NULL);

    nb = send(client->fd, &reply, sizeof(reply), 0);
    if (nb != sizeof(reply)) {
        RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                __func__, (uint32_t)sizeof(reply), nb);
        return -1;
    }

    return 0;
}

static int
handle_cmd_exit(struct natasha_client *client, struct core *cores,
                  uint8_t cmd_type)
{
    struct natasha_cmd_reply reply;
    int nb;

    reply.type = cmd_type;
    reply.status = NATASHA_REPLY_OK;

    nb = send(client->fd, &reply, sizeof(reply), 0);
    if (nb != sizeof(reply)) {
        RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                __func__, (uint32_t)sizeof(reply), nb);
        return -1;
    }
    /* Send SIGTERM */
    raise(SIGTERM);

    return 0;
}

static int
handle_cmd_reset_stats(struct natasha_client *client, struct core *cores,
                       uint8_t cmd_type)
{
    struct natasha_cmd_reply reply;
    uint8_t port;
    int ret;
    int nb;

    reply.type = cmd_type;
    reply.status = NATASHA_REPLY_OK;

    for (port = 0; port < rte_eth_dev_count(); ++port)
        if ((ret = rte_eth_stats_reset(port)))
            reply.status = ret;

    nb = send(client->fd, &reply, sizeof(reply), 0);
    if (nb != sizeof(reply)) {
        RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                __func__, (uint32_t)sizeof(reply), nb);
        return -1;
    }

    return 0;
}

static int
handle_cmd_version(struct natasha_client *client, struct core *cores,
                   uint8_t cmd_type)
{
    struct natasha_cmd_reply reply;
    char version[] = GIT_VERSION;
    size_t data_size = strlen(version);
    int nb;

    reply.type = cmd_type;
    reply.status = NATASHA_REPLY_OK;
    reply.data_size = rte_cpu_to_be_16(data_size);

    nb = send(client->fd, &reply, sizeof(reply) , 0);
    if (nb != sizeof(reply)) {
        RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                __func__, (uint32_t)sizeof(reply), nb);
        return -1;
    }

    nb = send(client->fd, version, data_size , 0);
    if (nb != data_size) {
        RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                __func__, (uint32_t)data_size, nb);
        return -1;
    }

    return 0;
}

void
cpu_to_be_port_stats(struct rte_eth_stats *port) {

    port->ipackets = rte_cpu_to_be_64(port->ipackets);
    port->opackets = rte_cpu_to_be_64(port->opackets);
    port->ibytes = rte_cpu_to_be_64(port->ibytes);
    port->obytes = rte_cpu_to_be_64(port->obytes);
    port->imissed = rte_cpu_to_be_64(port->imissed);
    port->ierrors = rte_cpu_to_be_64(port->ierrors);
    port->oerrors = rte_cpu_to_be_64(port->oerrors);
    port->rx_nombuf = rte_cpu_to_be_64(port->rx_nombuf);

    /* queues stats are ignored */
}
static int
handle_cmd_dpdk_stats(struct natasha_client *client, struct core *cores,
                      uint8_t cmd_type)
{
    struct rte_eth_stats port_stats[rte_eth_dev_count()];
    struct natasha_cmd_reply reply;
    size_t data_size;
    uint8_t port;
    int nb;


    reply.status = NATASHA_REPLY_OK;
    for (port = 0; port < rte_eth_dev_count(); ++port) {
        if (rte_eth_stats_get(port, &port_stats[port]) != 0) {
            RTE_LOG(ERR, APP, "Port %i: unable to get stats: %s\n",
                    port, rte_strerror(rte_errno));
            reply.status = -1;
        }
        cpu_to_be_port_stats(&port_stats[port]);
    }

    data_size = sizeof(port_stats);

    reply.type = cmd_type;
    reply.data_size = rte_cpu_to_be_16(data_size);

    nb = send(client->fd, &reply, sizeof(reply) , 0);
    if (nb != sizeof(reply)) {
        RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                __func__, (uint32_t)sizeof(reply), nb);
        return -1;
    }

    /* Send stats data structures */
    nb = send(client->fd, port_stats, data_size , 0);
    if (nb != data_size) {
        RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                __func__, (uint32_t)data_size, nb);
        return -1;
    }

    return 0;
}

void
cpu_to_be_app_stats(struct natasha_app_stats *core) {

    core->drop_no_rule = rte_cpu_to_be_64(core->drop_no_rule);
    core->drop_nat_condition = rte_cpu_to_be_64(core->drop_nat_condition);
    core->drop_bad_l3_cksum = rte_cpu_to_be_64(core->drop_bad_l3_cksum);
    core->rx_bad_l4_cksum = rte_cpu_to_be_64(core->rx_bad_l4_cksum);
    core->drop_unknown_icmp = rte_cpu_to_be_64(core->drop_unknown_icmp);
    core->drop_unhandled_ethertype = rte_cpu_to_be_64(core->drop_unhandled_ethertype);
    core->drop_tx_notsent = rte_cpu_to_be_64(core->drop_tx_notsent);

}

static int
handle_cmd_app_stats(struct natasha_client *client, struct core *cores,
                      uint8_t cmd_type)
{
    struct natasha_app_stats core_stats;
    struct natasha_cmd_reply reply;
    size_t data_size;
    uint8_t coreid;
    int nb;


    reply.status = NATASHA_REPLY_OK;
    /* Don't count the master as is idle */
    data_size = (sizeof(core_stats) + sizeof(coreid)) * (rte_lcore_count() - 1);

    reply.type = cmd_type;
    reply.data_size = rte_cpu_to_be_16(data_size);

    nb = send(client->fd, &reply, sizeof(reply) , 0);
    if (nb != sizeof(reply)) {
        RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                __func__, (uint32_t)sizeof(reply), nb);
        return -1;
    }

    RTE_LCORE_FOREACH_SLAVE(coreid) {
        memcpy(&core_stats, cores[coreid].stats,
               sizeof(core_stats));
        cpu_to_be_app_stats(&core_stats);
        /* Send coreID  uint8_t */
        nb = send(client->fd, &coreid, sizeof(coreid) , 0);
        if (nb != sizeof(coreid)) {
            RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                    __func__, (uint32_t)sizeof(core_stats), nb);
            return -1;
        }
        /* Send core stats */
        nb = send(client->fd, &core_stats, sizeof(core_stats) , 0);
        if (nb != sizeof(core_stats)) {
            RTE_LOG(ERR, APP, "%s: failed to send 0x%x bytes (sent 0x%x bytes)\n",
                    __func__, (uint32_t)sizeof(core_stats), nb);
            return -1;
        }
    }

    return 0;
}

const struct natasha_command natasha_commands[] = {
    {
        .cmd_type = NATASHA_CMD_STATUS,
        .func = handle_cmd_status,
    },
    {
        .cmd_type = NATASHA_CMD_EXIT,
        .func = handle_cmd_exit,
    },
    {
        .cmd_type = NATASHA_CMD_RELOAD,
        .func = handle_cmd_reload,
    },
    {
        .cmd_type = NATASHA_CMD_RESET_STATS,
        .func = handle_cmd_reset_stats,
    },
    {
        .cmd_type = NATASHA_CMD_VERSION,
        .func = handle_cmd_version,
    },
    {
        .cmd_type = NATASHA_CMD_DPDK_STATS,
        .func = handle_cmd_dpdk_stats,
    },
    {
        .cmd_type = NATASHA_CMD_APP_STATS,
        .func = handle_cmd_app_stats,
    },
};

static int
handle_client_query(struct natasha_client *client, struct core *cores)
{
    struct natasha_query *query = NULL;
    size_t len;
    int i;

    query = (struct natasha_query *)client->buf;

    len = sizeof(natasha_commands) / sizeof(struct natasha_command);
    for (i = 0; i < len; i++)
        if (natasha_commands[i].cmd_type == query->type)
            return natasha_commands[i].func(client, cores, query->type);

    RTE_LOG(WARNING, APP,
            "Server received unknown query, connot handle command type = 0x%x",
            query->type);
    return -1;
}

static void
disconnect_client(struct natasha_client *client)
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
    struct natasha_client clients[NATASHA_MAX_CLIENTS];
    int slaves_alive;
    int cur_clients;
    fd_set readfds;
    int events;
    int maxfd;

    memset(&clients, 0, sizeof(clients));
    cur_clients = 0;
    slaves_alive = 0;

    while (1) {
        size_t i;
        struct timeval timeout;

        FD_ZERO(&readfds);
        FD_SET(s, &readfds);
        maxfd = s;

        /* Adiust maxfd and add child sockets fd to readfds */
        for (i = 0; i < NATASHA_MAX_CLIENTS; ++i) {
            if (clients[i].fd) {
                FD_SET(clients[i].fd, &readfds);
                if (clients[i].fd >= maxfd) {
                    maxfd = clients[i].fd;
                }
            }
        }

        /* Setup timeout */
        memset(&timeout, 0, sizeof(timeout));
        timeout.tv_sec = 1;

        events = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if ((events < 0) && (errno != EINTR)) {
            RTE_LOG(ERR, APP,
                    "Adm server: cannot select on adm socket: %s\n",
                    strerror(errno));
            return EXIT_FAILURE;
        }

        /* if slaves aren't alive, quit */
        check_slaves_alive(&slaves_alive);

        /* New client */
        if (FD_ISSET(s, &readfds)) {
            struct sockaddr_un client;
            int cs;
            socklen_t len;

            /* Accept */
            if ((cs = accept(s, (struct sockaddr *)&client, &len)) < 0) {
                RTE_LOG(ERR, APP, "Adm server: new client accept error: %s\n",
                        strerror(errno));
            } else {
                /* Too many connections, reject client */
                if (cur_clients >= NATASHA_MAX_CLIENTS) {
                    RTE_LOG(
                        ERR, APP,
                        "Adm server: reject client (too many connections)\n"
                    );
                    close(cs);
                } /* Set client socket non blocking */
                else if (fcntl(cs, F_SETFL, O_NONBLOCK) < 0) {
                    RTE_LOG(
                        ERR, APP,
                        "Adm server: reject client (can't ENONBLOCK)\n"
                    );
                    close(cs);
                } /* Everything ok, append client */
                else {
                    for (i = 0; i < NATASHA_MAX_CLIENTS; ++i) {
                        if (clients[i].fd == 0) {
                            clients[i].fd = cs;
                            ++cur_clients;
                            break;
                        }
                    }
                }
            }
        }

        // Read clients commands
        for (i = 0; i < NATASHA_MAX_CLIENTS; ++i) {
            if (FD_ISSET(clients[i].fd, &readfds)) {
                ssize_t nbread;

                nbread = read(clients[i].fd, clients[i].buf,
                              sizeof(clients[i].buf));

                if (nbread < 0) {
                    RTE_LOG(ERR, APP, "Adm server: client read error: %s\n",
                            strerror(errno));
                    disconnect_client(&clients[i]);
                    --cur_clients;
                }
                /* client disconnection */
                else if (nbread == 0) {
                    disconnect_client(&clients[i]);
                    --cur_clients;
                }
                else if (sizeof(struct natasha_query) != nbread) {
                    RTE_LOG(ERR, APP,
                            "Adm server: Receiving data lenght from client error,"
                            "expected 0x%x received 0x%x\n",
                            (uint32_t)sizeof(struct natasha_query),
                            (uint32_t)nbread);
                    disconnect_client(&clients[i]);
                    --cur_clients;
                } else {
                    if (handle_client_query(&clients[i], cores) < 0) {
                        disconnect_client(&clients[i]);
                        --cur_clients;
                    }
                }
                break;
            }
        }
    }

    return EXIT_SUCCESS;
}

/*
 * Run the administration server forerver.
 */
int
adm_server(struct core *cores, int argc, char **argv)
{
    int s;
    struct sockaddr_in addr;
    const int yes = 1;

    /*
     * Make write() return -1 instead of raising SIGPIPE if we write to a
     * disconnected client.
     */
    signal(SIGPIPE, SIG_IGN);

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        RTE_LOG(ERR, APP, "Cannot create adm socket: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        RTE_LOG(ERR, APP, "Cannot setsockopt adm socket: %s\n",
                strerror(errno));
        close(s);
        return EXIT_FAILURE;
    }

    if (fcntl(s, F_SETFL, O_NONBLOCK) < 0) {
        RTE_LOG(ERR, APP, "Cannot set non block on socket: %s\n",
                strerror(errno));
        close(s);
        return EXIT_FAILURE;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(NATASHA_SOCKET_PORT);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        RTE_LOG(ERR, APP, "Cannot bind adm socket: %s\n", strerror(errno));
        close(s);
        return EXIT_FAILURE;
    }

    if (listen(s, NATASHA_MAX_CLIENTS) < 0) {
        RTE_LOG(ERR, APP, "Cannot listen on adm socket: %s\n",
                strerror(errno));
        close(s);
        return EXIT_FAILURE;
    }

    return adm_loop(s, cores, argc, argv);
}
