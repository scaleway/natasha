/*
 * Try to load configuration a lot of times, and verify ressources are freed
 * and closed correctly.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

#include <rte_malloc.h>

#include "natasha.h"


/*
 * Returns the number of open descriptors from /proc/self/fd.
 */
int
number_open_descs(void)
{
    DIR *dir;
    struct dirent *dirent;
    int n;

    if ((dir = opendir("/proc/self/fd/")) == NULL) {
        perror("opendir");
        return -1;
    }

    n = 0;
    while ((dirent = readdir(dir)) != NULL) {
        ++n;
    }

    closedir(dir);
    return n;
}


/*
 * Like rte_malloc_dump_stats(), but returns the sum of heap_freesz_bytes for
 * each NUMA node.
 */
size_t
heap_freesize(void)
{
    unsigned int socket;
    struct rte_malloc_socket_stats sock_stats;
    size_t size;

    size = 0;
	for (socket = 0; socket < RTE_MAX_NUMA_NODES; socket++) {
        if ((rte_malloc_get_socket_stats(socket, &sock_stats) < 0)) {
			continue;
        }
        size += sock_stats.heap_freesz_bytes;
    }
    return size;
}


int
main(int argc, char **argv)
{
    int ret;
    struct app_config app_config = {};
    size_t i;
    int initial_fds, final_fds;
    size_t initial_freesize, final_freesize;

    if ((ret = rte_eal_init(argc, argv)) < 0) {
        fprintf(stderr, "Error with EAL initialization\n");
        exit(1);
    }

    argc -= ret;
    argv += ret;

    initial_fds = number_open_descs();
    initial_freesize = heap_freesize();

    for (i = 0; i < 100; ++i) {
        if (app_config_load(&app_config, argc, argv, SOCKET_ID_ANY) < 0) {
            fprintf(stderr, "Unable to load configuration\n");
            exit(1);
        }
        app_config_free(&app_config);
    }

    if (initial_fds != (final_fds = number_open_descs()))  {
        fprintf(stderr, "File descriptors leak: %i fd were open before loading "
                "config, now %i fds (%+i fds)\n", initial_fds,
                final_fds, final_fds - initial_fds);
        exit(EXIT_FAILURE);
    }

    if (initial_freesize != (final_freesize = heap_freesize()))  {
        fprintf(stderr, "Memory leak: %lu freesize before loading config, "
                "now %lu (%+li)\n", initial_freesize, final_freesize,
                final_freesize - initial_freesize);
        exit(EXIT_FAILURE);
    }

    return 0;
}
