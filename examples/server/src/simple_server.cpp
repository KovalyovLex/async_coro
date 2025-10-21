#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  int port = 8080;
  if (argc > 1) port = std::atoi(argv[1]);

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    return 1;
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return 1;
  }

  if (listen(server_fd, 16) < 0) {
    perror("listen");
    return 1;
  }

  std::cout << "simple_server listening on port " << port << "\n";

  while (true) {
    int client = accept(server_fd, nullptr, nullptr);
    if (client < 0) {
      perror("accept");
      continue;
    }

    const char* resp = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nHello";
    send(client, resp, strlen(resp), 0);
    close(client);
  }

  close(server_fd);
  return 0;
}
