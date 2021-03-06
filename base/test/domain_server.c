#include <stdio.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "tsbase.h"


poller_t *cur_poller = &epoll_poller;
fd_list_t *g_con;
int g_fd_size;

int handle_read(dlist_t *read)
{
    int evs = poller_do_poll();
    if(evs == 0)
        return -1;

    ev_events_t *events = poller_get_events();
    for(int i = 0; i < evs; i++)
    {
        int fd = events[i].fd;
        int type = g_con[fd].type;
        connection_t *pcon = g_con[fd].con;
        
        if(type == CON_T_LISTEN)
        {
            int acc = accept_client(pcon);
            printf("accept_client fd = %d\n", acc);
        }
        else if(type == CON_T_SERVER || type == CON_T_CLIENT)
        {
            printf("recv_data, fd = %d\n", fd);
            recv_data(pcon);
            dlist_insert_tail(read, pcon);
        }

        if(pcon->con_status == CON_S_BROKEN)
        {
            poller_del_fd(fd);
            close(fd);
        }
    }

    return 0;
}

int handle_write(dlist_t *write)
{
    dlist_entry_t *item;
    list_for_each(item, write->head, write->tail)
    {
        connection_t *con = (connection_t*)item->data;
        send_data(con);
    }

    dlist_delete_all(write, DLIST_DONOT_FREE_DATA);

    return 0;
}

int process_data(dlist_t *read, dlist_t *write)
{
    dlist_entry_t *item;
    list_for_each(item, read->head, read->tail)
    {
        connection_t *con = (connection_t*)item->data;
        buffer_t *rcvbuf = con->rcvbuf;
        buffer_t *sndbuf = con->sndbuf;

        const char *readptr = rcvbuf->get_read_ptr(rcvbuf);
        int datalen = rcvbuf->get_data_size(rcvbuf);

        char *writeptr = sndbuf->get_write_ptr(sndbuf);

        memcpy(writeptr, readptr, datalen);
        char buff[64] = {0};
        memcpy(buff, readptr, datalen);
        printf("recv data: %s\n", buff);
        rcvbuf->set_read_size(rcvbuf ,datalen);
        sndbuf->set_write_size(sndbuf ,datalen);
    
        dlist_insert_tail(write, con);
    }

    dlist_delete_all(read, DLIST_DONOT_FREE_DATA);

    return 0;
}

int main(int argc, char* argv[])
{
    g_fd_size = SOCK_MIN_FD_SIZE;
    g_con = (fd_list_t*)malloc(sizeof(fd_list_t) * g_fd_size); 

    for(int i = 0; i < g_fd_size; i++)
    {
        g_con[i].con = NULL;
        g_con[i].type = -1;
    }

    int fd = start_listen_un("/data/lqb/tscode/tsbase/test/deamon_socket"); 
    if(fd < 0)
    {
        printf("start_listen_ipv4 failed.\n");
        return -1;
    }

    poller_ev_init();
    poller_add_fd(fd, EV_READ);

    dlist_t *read = dlist_create();
    dlist_t *write = dlist_create();

    while(1)
    {
        handle_read(read);
        process_data(read, write);
        handle_write(write);
    }
}



