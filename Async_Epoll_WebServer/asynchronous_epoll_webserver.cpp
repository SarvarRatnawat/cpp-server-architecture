#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h> 
#include <vector>
#include<sys/mman.h>    
#include<sys/stat.h>
#include<sys/sendfile.h>
#include<cerrno>
#include<unordered_map>
 
#define MAX_EVENTS 1000
 
struct file_sending_stats
{
    int file_to_send_fd;
    off_t current_offset;
    off_t bytes_to_send;
    off_t file_size;
};
 
std::unordered_map<int, file_sending_stats> clients_file_memory;
 
void save_logs_to_file(const std::string &message)
{
    const char *file_path = "async_webserver2.2_logs.txt";
 
    int file_fd = open(file_path, O_RDWR | O_CREAT, 0666);
    if(file_fd < 0)
    {
        std::cerr << "unable to open log file!\n";
        return;
    }
 
    struct stat file_info;
    if(fstat(file_fd, &file_info) < 0)
    {
        std::cerr << "unable to get log_file info!\n";
        close(file_fd);
        return;
    }
 
    off_t old_file_size = file_info.st_size;
 
    size_t log_size = message.length();
 
    if(ftruncate(file_fd, (old_file_size + log_size)) < 0)
    {
        std::cerr << "ERROR (truncating) while managing log file!\n";
        close(file_fd);
        return;
    }
 
    off_t new_file_size = old_file_size + log_size;
 
    char *mapped_data = (char *)mmap(NULL, new_file_size, PROT_READ | PROT_WRITE, MAP_SHARED, file_fd, 0);
    if(mapped_data == MAP_FAILED)
    {
        std::cerr << "ERROR (mmap failed) while managing log file!\n";
        close(file_fd);
        return;
    }
 
    memcpy((mapped_data + old_file_size), message.c_str(), log_size);
 
    if(munmap(mapped_data, new_file_size) < 0)
    {
        std::cerr << "ERROR (unmap) while managing the file!\n";
        close(file_fd);
        return;
    }
 
    close(file_fd);
}

void make_socket_non_blocking(int socket_fd) 
{
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if(flags == -1) 
    {
        std::cerr << "fcntl(F_GETFL) failed for fd " << socket_fd << "\n";
        return;
    }
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
}

void finish_client_transfer(int epoll_fd, int client_fd) 
{
    auto it = clients_file_memory.find(client_fd);
    if(it != clients_file_memory.end()) 
    {
        close(it->second.file_to_send_fd);
        clients_file_memory.erase(it);
    }
 
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
}

void attempt_send(int epoll_fd, int active_fd) 
{
    auto it = clients_file_memory.find(active_fd);
    if(it == clients_file_memory.end())
    {
        return;
    }
 
    file_sending_stats &state = it->second;
 
    ssize_t bytes_sent = sendfile(active_fd, state.file_to_send_fd,
                                   &state.current_offset, state.bytes_to_send);
 
    if(bytes_sent > 0) 
    {
        state.bytes_to_send -= bytes_sent;
 
        if(state.bytes_to_send == 0)
        {
            std::string log = "complete sending file to user with fd: " + std::to_string(active_fd) + "\n";
            save_logs_to_file(log);
            finish_client_transfer(epoll_fd, active_fd);
        } 
        else
        {
            struct epoll_event ev;
            ev.data.fd = active_fd;
            ev.events = EPOLLOUT;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, active_fd, &ev);
        }
    }
    else if(bytes_sent < 0) 
    {
        if(errno == EAGAIN || errno == EWOULDBLOCK) 
        {
            struct epoll_event ev;
            ev.data.fd = active_fd;
            ev.events = EPOLLOUT;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, active_fd, &ev);
        } 
        else 
        {
            std::cerr << "sendfile error for fd " << active_fd << ": " << strerror(errno) << "\n";
            finish_client_transfer(epoll_fd, active_fd);
        }
    }
}
 
int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
 
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
 
    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, SOMAXCONN);

    make_socket_non_blocking(server_fd);

    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1)
    {
        return 1;
    }

    struct epoll_event event;
    event.data.fd = server_fd;
    event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    struct epoll_event events[MAX_EVENTS];
 
    std::cout << "Asynchronous Epoll Server running on Port 8080 (Single Thread!)...\n\n";

    while(true)
    {
        int num_ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for(int i = 0; i < num_ready; i++) 
        {
            int active_fd = events[i].data.fd;

            if(active_fd == server_fd)
            {
                while(true)
                {
                    struct sockaddr_in client_address;
                    socklen_t addrlen = sizeof(client_address);
                    int child_fd = accept(server_fd, (struct sockaddr*)&client_address, &addrlen);
                    
                    if (child_fd == -1)
                    {
                        break;
                    }
 
                    std::cout << "[Kernel] New connection! FD: " << child_fd << "\n";
                    
                    make_socket_non_blocking(child_fd);
                    event.data.fd = child_fd;
                    event.events = EPOLLIN;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, child_fd, &event);
 
                    std::string log = "user with IP: " + std::string(inet_ntoa(client_address.sin_addr)) + " connected and FD is: " + std::to_string(child_fd) + ".\n";   // "inet_ntoa()" returns "char*"
                    save_logs_to_file(log);
                }
            }

            else if(events[i].events & EPOLLOUT)
            {
                attempt_send(epoll_fd, active_fd);
            }
            
            else
            {
                char buffer[4096];
                memset(buffer, 0, sizeof(buffer));
                int bytes_read = read(active_fd, buffer, sizeof(buffer));
 
                if(bytes_read <= 0) 
                {
                    std::cout << "[Kernel] Client FD " << active_fd << " disconnected.\n";
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_fd, NULL);
                    close(active_fd);
                } 
                else 
                {
                    // THE ROUTER
                    std::string request(buffer);
 
                    if(request.find("GET / HTTP/1.1") != std::string::npos)
                    {
                        // ROUTE 1- Home Page
                        std::string html = "<h1>Asynchronous Server</h1><p>Go to <a href='/logs'>/logs</a> to see mmap in action!</p>";
                        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n" + html;
                        write(active_fd, response.c_str(), response.length());
                    } 
                    else if(request.find("GET /logs HTTP/1.1") != std::string::npos) 
                    {
                        std::string log = "user with fd: " + std::to_string(active_fd) + " asked for logs.\n";
                        save_logs_to_file(log);
 
                        // ROUTE 2- log file
                        int file_fd = open("mmap_database_test.txt", O_RDONLY);
    
                        if(file_fd < 0)
                        {
                            std::string err = "HTTP/1.1 404 Not Found\r\n\r\n<h1>No database found.</h1>";
                            write(active_fd, err.c_str(), err.length());

                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_fd, NULL);
                            close(active_fd);
                            continue;
                        }
 
                        struct stat file_info;
                        if(fstat(file_fd, &file_info) < 0)
                        {
                            std::string err = "HTTP/1.1 404 Not Found\r\n\r\n<h1>Couldn't get data.</h1>";
                            write(active_fd, err.c_str(), err.length());
                            
                            close(file_fd);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_fd, NULL);
                            close(active_fd);
                            
                            continue;
                        }
                        off_t file_size = file_info.st_size;
 
                        file_sending_stats file_stat;
 
                        file_stat.file_to_send_fd = file_fd;
                        file_stat.bytes_to_send = file_size;
                        file_stat.current_offset = 0;
                        file_stat.file_size = file_size;
 
                        clients_file_memory[active_fd] = file_stat;
 
                        std::string header = "HTTP/1.1 200 OK\r\n";
                        header += "Content-Type: text/plain\r\n"; 
                        header += "Content-Length: " + std::to_string(file_size) + "\r\n";
                        header += "\r\n"; 

                        write(active_fd, header.c_str(), header.length());
 
                        attempt_send(epoll_fd, active_fd);
                        continue;
                    }
 
                    else 
                    {
                        // ROUTE 3- 404 Error
                        std::string err = "HTTP/1.1 404 Not Found\r\n\r\n<h1>404 Page Not Found</h1>";
                        write(active_fd, err.c_str(), err.length());
                    }
 
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_fd, NULL);
                    close(active_fd);
                }
            }
        }
    }
 
    close(server_fd);
    return 0;
}