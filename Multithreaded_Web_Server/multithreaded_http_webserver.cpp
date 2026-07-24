#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <thread>
#include <fstream> 

void save_to_database(const std::string& log_entry)
{
    std::ofstream db_file("database.txt", std::ios::app);
    
    if(db_file.is_open())
    {
        db_file << log_entry << "\n";
        db_file.close();
    } 
    else
    {
        std::cerr << "CRITICAL ERROR: Could not spin up the hard drive!\n";
    }
}

void handle_client(int child_socket_fd, std::string client_ip) 
{
    std::cout << "[THREAD] handling client with fd: " << child_socket_fd << "\n";

    char buffer[4096]; 
    memset(buffer, 0, sizeof(buffer)); 
    read(child_socket_fd, buffer, 4096);
    
    std::string request(buffer); 

    std::string http_response;

    // THE ROUTER
    if(request.find("GET / HTTP/1.1") != std::string::npos)
    {
        std::string html = "<h1>Welcome!</h1><p>Check the server terminal, I just saved your IP to the hard drive.</p>";
        http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n" + html;
        
        // Save the visit to the file.
        save_to_database("Home Page visited by IP: " + client_ip);
        std::cout << "[Database] Saved visit from " << client_ip << " to hard drive.\n";

    }
    else if(request.find("GET /logs HTTP/1.1") != std::string::npos)
    {
        
        // Read logs from the file.
        std::ifstream db_file("database.txt");
        std::string all_logs = "";
        std::string line;
        
        if(db_file.is_open())
        {
            while(getline(db_file, line))
            {
                all_logs += line + "<br>";
            }
            db_file.close();
        }
        else
        {
            all_logs = "Database is empty!";
        }

        std::string html = "<h1>Server Logs (From Hard Drive)</h1><p>" + all_logs + "</p>";
        http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n" + html;  
    }
    else
    {
        std::string html = "<h1>404 Not Found</h1>";
        http_response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n" + html;
    }

    write(child_socket_fd, http_response.c_str(), http_response.length());
    close(child_socket_fd);
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
        return 1;
    }

    if(listen(server_fd, 10) < 0)
    {
        return 1;
    }
    
    std::cout << "Web Server with Persistence running on Port 8080...\n\n";

    while (true)
    {
        struct sockaddr_in client_address;
        socklen_t addrlen = sizeof(client_address);

        int child_fd = accept(server_fd, (struct sockaddr*)&client_address, &addrlen);
        if (child_fd < 0)
        {
            continue;
        }

        std::string client_ip = inet_ntoa(client_address.sin_addr);

        std::cout << "accepted new request with IP: " << client_ip << "\n";
        std::cout << "kernal created new socket with FD: " << child_fd << "\n";

        std::thread client_thread(handle_client, child_fd, client_ip);
        client_thread.detach(); 
    }

    close(server_fd); 
    return 0;
}