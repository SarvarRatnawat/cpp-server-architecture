#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <string.h>
#include <vector>
#include <mutex>
#include <algorithm>

std::vector<int> active_clients;
std::mutex clients_mutex;

void broadcast_message(const std::string& message, int sender_fd) 
{
    std::lock_guard<std::mutex> lock(clients_mutex); 
    
    for(int client_fd : active_clients)
    {
        if(client_fd != sender_fd)
        {
            write(client_fd, message.c_str(), message.length());
        }
    }
}

void handle_client(int child_socket_fd)
{
    std::cout << "[Thread] Handling client on FD: " << child_socket_fd << "\n";
    
    // ADD CLIENT
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        active_clients.push_back(child_socket_fd);
    }
    
    char buffer[1024];
    
    while(true)
    {
        memset(buffer, 0, sizeof(buffer)); 
        int valread = read(child_socket_fd, buffer, 1024);
        
        if(valread <= 0)
        {
            std::cout << "[Thread] Client on FD " << child_socket_fd << " disconnected.\n";
            break; 
        }

        std::cout << "[Client FD " << child_socket_fd << "]: " << buffer << "\n";

        // BROADCAST
        std::string broadcast_msg = "[User " + std::to_string(child_socket_fd) + "]: " + std::string(buffer);
        broadcast_message(broadcast_msg, child_socket_fd);
    }

    // REMOVE CLIENT
    {
        std::lock_guard<std::mutex> lock(clients_mutex);

        active_clients.erase(std::remove(active_clients.begin(), active_clients.end(), child_socket_fd), active_clients.end());
    }

    close(child_socket_fd);
    std::cout << "[Thread] FD " << child_socket_fd << " removed from roster and closed.\n";
}

int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0)
    {
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if(bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        std::cerr << "Bind failed!\n";
        return 1;
    }

    if(listen(server_fd, 10) < 0)
    {
        return 1;
    }

    std::cout << "Broadcast Chat Server listening on Port 8080...\n\n";

    while(true)
    {
        struct sockaddr_in client_address;
        socklen_t addrlen = sizeof(client_address);

        int child_fd = accept(server_fd, (struct sockaddr*)&client_address, &addrlen);
        if(child_fd < 0)
        {
            continue;
        }

        char* client_ip = inet_ntoa(client_address.sin_addr);
        int client_port = ntohs(client_address.sin_port);

        std::cout << "*** User Connected! IP: " << client_ip << " | Port: " << client_port << " | FD: " << child_fd << " ***\n";

        std::thread client_thread(handle_client, child_fd);
        client_thread.detach(); 
    }

    close(server_fd); 
    return 0;
}