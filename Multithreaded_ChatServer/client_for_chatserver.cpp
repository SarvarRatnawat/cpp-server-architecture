#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <thread>

// thread for listening incoming messages from the server
void receive_messages(int socket_fd) 
{
    char buffer[1024];
    while(true)
    {
        memset(buffer, 0, sizeof(buffer));
        
        int valread = read(socket_fd, buffer, 1024);
        
        if(valread <= 0) 
        {
            std::cout << "\n[Server disconnected or crashed]\n";
            exit(0);
        }
        
        // Print received message.
        std::cout << "\r" << buffer << "\n";
        std::cout << "You: " << std::flush;
    }
}

int main() 
{
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(client_fd < 0) 
    {
        std::cerr << "failed to create scocket!\n";
        return 1;
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(8080);
    if(inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr) <= 0) 
    {
        std::cerr << "invalid or incorrect address/ address not supported!\n";
        return 1;
    }

    std::cout << "Attempting to connect...\n";
    if(connect(client_fd, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) 
    {
        return 1;
    }
    std::cout << "Connected successfully! Type 'quit' to exit.\n\n";

    // listening thread
    std::thread receiver_thread(receive_messages, client_fd);
    receiver_thread.detach(); 

    // keyboard input
    std::string user_input;
    while(true) 
    {
        std::cout << "You: ";
        std::getline(std::cin, user_input);

        if(user_input == "quit") 
        {
            break;
        }

        write(client_fd, user_input.c_str(), user_input.length());
    }

    close(client_fd);
    return 0;
}