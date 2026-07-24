#include<iostream>
#include<sys/socket.h>
#include<sys/fcntl.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<string.h>
#include<unistd.h>
#include<vector>
#include<algorithm>
#include<fstream>

#define MAX_EVENTS 1000

std::vector<int>  active_clients;

void make_socket_non_blocking(int socket_fd);
void broadcast_data(std::string &data);
void broadcast_data(std::string &data, int sock_fd);
void save_logs(struct sockaddr_in &connection);

int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0)
    {
        std::cerr << "failed to create listening socket!";
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    make_socket_non_blocking(server_fd);

    int epoll_fd = epoll_create1(0);

    struct epoll_event event;
    event.data.fd = server_fd;
    event.events = EPOLLIN;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    struct sockaddr_in server_addrs;
    server_addrs.sin_family = AF_INET;
    server_addrs.sin_addr.s_addr = INADDR_ANY;
    server_addrs.sin_port = htons(8080);


    if(bind(server_fd, (struct sockaddr *)&server_addrs, sizeof(server_addrs)) < 0)
    {
        std::cerr << "failed to bind to port " << ntohs(server_addrs.sin_port) << "!";
        return 1;
    }

    if(listen(server_fd, SOMAXCONN) < 0)
    {
        std::cerr << "failed on listening for server!";
        return 1;
    }

    struct epoll_event events[MAX_EVENTS];

    std::cout << "asynchronous server running on port 8080 ...\n\n";

    while(1)
    {
        int num_ready_sock = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if(num_ready_sock == -1)
        {
            std::cerr << "ERROR! epoll_wait failed!";
            break;
        }

        for(int i = 0; i < num_ready_sock; i++)
        {
            int active_fd = events[i].data.fd;

            if(active_fd == server_fd) // means new connection requests
            {
                while(1)
                {
                    struct sockaddr_in client_addrs;
                    socklen_t client_addrs_len = sizeof(client_addrs);

                    int new_connec_fd = accept(server_fd, (sockaddr *)&client_addrs, &client_addrs_len);
                    if(new_connec_fd == -1)
                    {
                        break;
                    }

                    std::cout << "[KERNEL] new connection, IP: " << inet_ntoa(client_addrs.sin_addr) << ", port: " << ntohs(client_addrs.sin_port) << ", FD: " << new_connec_fd << ".\n";

                    std::string server_msg = "[server] new user connected, FD: " + std::to_string(new_connec_fd) + ".\n";
                    broadcast_data(server_msg);

                    active_clients.push_back(new_connec_fd);

                    save_logs(client_addrs);

                    make_socket_non_blocking(new_connec_fd);

                    event.data.fd = new_connec_fd;
                    event.events = EPOLLIN;

                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_connec_fd, &event) == -1)
                    {
                        std::cerr << "ERROR! epoll_ctl ADD failed!";
                        close(new_connec_fd);
                    }
                }
            }
            else
            {
                char buffer[1024];
                memset(buffer,  0, sizeof(buffer));

                int byte_read = read(active_fd, &buffer, sizeof(buffer));
                if(byte_read <= 0)
                {
                    std::cout << "[kernal] client with fd: " << active_fd << " disconnected.\n";
                    std::string server_msg = "[SERVER] client with fd:" + std::to_string(active_fd) + " disconnected.\n";

                    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_fd, NULL) == -1)
                    {
                        std::cerr << "ERROR! epoll_ctl DEL failed!";
                    }
                    close(active_fd);

                    active_clients.erase(std::remove(active_clients.begin(), active_clients.end(), active_fd), active_clients.end());

                    broadcast_data(server_msg);
                }
                

                else
                {
                    std::string message = "[user FD: " + std::to_string(active_fd) + "]: " + std::string(buffer) + "\n";

                    std::cout << message;
                    broadcast_data(message, active_fd);
                }
            }
        }
    }

    close(server_fd);
    return 0;
}

void make_socket_non_blocking(int socket_fd)
{
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if(flags == -1)
    {
        std::cerr << "ERROR! fcntl F_GETFL failed!";
        return;
    }
    if(fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        std::cerr << "ERROR! fcntl F_SETFL failed!";
        return;
    }
}

void broadcast_data(std::string &data) // broadcasts message to all connected users
{
    int num_active_clients = active_clients.size();
    for(int i = 0; i < num_active_clients; i++)
    {
        ssize_t written = write(active_clients[i], data.c_str(), data.length());
        if(written < 0)
        {
            std::cerr << "ERROR! write to client fd " << active_clients[i] << " failed!";
        }
    }
}

void broadcast_data(std::string &data, int sock_fd) // broadcasts message to all clients except one.
{
    int num_active_clients = active_clients.size();
    for(int i = 0; i < num_active_clients; i++)
    {
        if(active_clients[i] == sock_fd)
        {
            continue;
        }

        ssize_t written = write(active_clients[i], data.c_str(), data.length());
        if(written < 0)
        {
            std::cerr << "ERROR! write to client fd " << active_clients[i] << " failed!";
        }
    }
}

void save_logs(struct sockaddr_in &connection)
{
    std::ofstream logs_file("async_chatserver1_logs.txt", std::ios::app);

    if(logs_file.is_open())
    {
        std::string line = "user with IP: " + std::string(inet_ntoa(connection.sin_addr)) + " connected.\n";
        logs_file << line;
        logs_file.close();
    }
    else
    {
        std::cerr << "ERROR! unable to open log file for the server!";
    }
}