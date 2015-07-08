#include "string.h"
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string>

const int BUF_SIZE = 1024;

//+----------------------------------------------------------------------------+
//| Main test function                                                         |
//+----------------------------------------------------------------------------+

int main() {

	/* Create TCP socket for handling incoming connections */
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == -1) {
        std::cout << strerror(errno) << std::endl;
        return -1;
    }

    /* Fill parameters */
    struct sockaddr_in sAddr;
    bzero(&sAddr, sizeof(sAddr));
    sAddr.sin_family = AF_INET;
    sAddr.sin_port   = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &(sAddr.sin_addr));

    /* Bind socket with specific parameters */
    int result = connect(fd, (struct sockaddr*)&sAddr, sizeof(sAddr));
    if (result == -1) {
        std::cout << strerror(errno) << std::endl;
        return -1;
    }

    std::string line;
    char buf[BUF_SIZE];

    /* Send request from cin */
    while (true) {
        std::getline(std::cin, line);
        line.append("\n");
        send(fd, line.c_str(), line.size(), 0);

        /* Read answer from server */
        ssize_t len = recv(fd, buf, BUF_SIZE, 0);
        if (len > 0) {
            printf("%s", buf);
        }
    }

	return 0;
}