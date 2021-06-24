#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>

typedef struct      s_client {
    int             fd;
    int             id;
    struct s_client *next;
}                   t_client;

int                 g_master_socket;
int                 g_id = 0;
fd_set              g_read_set;
fd_set              g_write_set;
fd_set              g_current_set;
t_client            *g_clients = NULL;

void        print_error(char *message);
void        print_fatal_error(void);
void        init_master_socket(int port);
void        start_mini_server(void);
int         create_client(void);
void        remove_client(int fd);
int         recv_message(int fd);
void        send_message(int fd, char *message);
int         get_max_fd(void);
int         get_id_client(int fd);

int main(int argc, char **argv) {
    if (argc != 2) {
        print_error("Wrong number of arguments\n");
    }
    init_master_socket(atoi(argv[1]));
    start_mini_server();
}

void print_error(char *message) {
    write(2, message, strlen(message));
    exit(1);
}

void print_fatal_error(void) {
    close(g_master_socket);
    print_error("Fatal error\n");
}

void init_master_socket(int port) {
    struct sockaddr_in servaddr;

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if ((g_master_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        print_fatal_error();
    }
    if (bind(g_master_socket, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        print_fatal_error();
    }
    if (listen(g_master_socket, 0) < 0) {
        print_fatal_error();
    }
}

void start_mini_server(void) {
    int max_fd;

    FD_ZERO(&g_current_set);
    FD_SET(g_master_socket, &g_current_set);
    while (1) {
        max_fd = get_max_fd();
        g_read_set = g_write_set = g_current_set;
        if (select(max_fd + 1, &g_read_set, &g_write_set, NULL, NULL) < 0) {
            continue;
        }
        for (int fd = 0; fd <= max_fd; fd++) { 
           if (FD_ISSET(fd, &g_read_set) == 0) {
                continue;
            }
            if (g_master_socket == fd) {
                FD_SET(create_client(), &g_current_set);
                break;
            }
            if (recv_message(fd)) {
                FD_CLR(fd, &g_current_set);
                break;
            }
        }
    }
}

int get_max_fd(void) {
    t_client *first_client;
    int max_fd;

    first_client = g_clients;
    max_fd = g_master_socket;
    while (first_client != NULL) {
        if (max_fd < first_client->fd) {
            max_fd = first_client->fd;
        }
        first_client = first_client->next;
    }
    return (max_fd);
}

int create_client(void) {
    t_client *new_client;
    int new_fd;
    struct sockaddr_in servaddr;
    socklen_t len;

    new_fd = accept(g_master_socket, (struct sockaddr *)&servaddr, &len);
    if (new_fd < 0) {
        print_fatal_error();
    }
    new_client = calloc(1, sizeof(t_client));
    if (new_client == NULL) {
        print_fatal_error();
    }
    new_client->id = g_id++;
    new_client->fd = new_fd;
    new_client->next = NULL;
    if (g_clients == NULL) {
        g_clients = new_client;
    } else {
        t_client *first_client = g_clients;
        while (first_client->next != NULL) {
            first_client = first_client->next;
        } 
        first_client->next = new_client;
    }
    char buffer[50];
    bzero(&buffer, sizeof(buffer));
    sprintf(buffer, "server: client %d just arrived\n", new_client->id);
    send_message(new_fd, buffer);
    return (new_fd);
}

int recv_message(int fd) {
    char message_recv[500000];
    char message_send[500000];
    char buffer[500000];

    bzero(&message_recv, sizeof(message_recv));
    bzero(&message_send, sizeof(message_send));
    bzero(&buffer, sizeof(buffer));
    if (recv(fd, message_recv, sizeof(message_recv), 0) <= 0) {
        remove_client(fd);
        return (1);
    }
    int i = 0;
    int k = 0;
    while (message_recv[i] != '\0') {
        buffer[k++] = message_recv[i];
        if (message_recv[i] == '\n') {
            sprintf(message_send, "client %d: %s", get_id_client(fd), buffer);
            send_message(fd, message_send);
            bzero(&message_send, sizeof(message_send));
            bzero(&buffer, sizeof(buffer));
            k = 0;
        }
        i++;
    }
    return (0);
}

void remove_client(int fd) {
    int id;
    t_client    *remove_client;

    if (g_clients == NULL) {
        return ;
    }
    id = get_id_client(fd);
    if (g_clients->id == id) {
        remove_client = g_clients;
        g_clients = g_clients->next;
        free(remove_client);
    } else {
        t_client    *first_client = g_clients;
        while (first_client != NULL) {
            if (first_client->next && first_client->next->id == id) {
                remove_client = first_client->next;
                first_client->next = first_client->next->next;
                free(remove_client);
                break;
            }
            first_client = first_client->next;
        }
    }
    char    buffer[50];
    sprintf(buffer, "server: client %d just left\n", id);
    send_message(fd, buffer);
    close(fd);
}

void        send_message(int fd, char *message) {
    t_client *first_client;

    first_client = g_clients;
    while (first_client != NULL) {
        if (first_client->id != get_id_client(fd) && FD_ISSET(first_client->fd, &g_write_set)) {
            if (send(first_client->fd, message, strlen(message), 0) < 0) {
                print_fatal_error();
            }
        }
        first_client = first_client->next;
    }
}

int         get_id_client(int fd) {
    t_client *first_client;

    first_client = g_clients;
    while (first_client != NULL) {
        if (first_client->fd == fd) {
            return (first_client->id);
        }
        first_client = first_client->next;
    }
    return (-1);
}
