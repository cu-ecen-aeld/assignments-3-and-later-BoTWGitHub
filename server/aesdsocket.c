#include <stdio.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>

#define OUTPUT_FILE "/var/tmp/aesdsocketdata"
#define PORT        "9000"

bool caught_signal = false;

static void signal_handler(int signal_number)
{
    if(signal_number == SIGINT || signal_number == SIGTERM) {
        caught_signal = true;
        syslog(LOG_DEBUG, "Caught signal, exiting");
        remove(OUTPUT_FILE);
    }
}

bool setting_signal()
{
    struct sigaction new_action;
    memset(&new_action, 0, sizeof(struct sigaction));
    new_action.sa_handler = signal_handler;

    if(sigaction(SIGINT, &new_action, NULL) != 0) {
        syslog(LOG_ERR, "Error registering for SIGINT");
        return false;
    }
    if(sigaction(SIGTERM, &new_action, NULL) != 0) {
        syslog(LOG_ERR, "Error registering for SIGTERM");
        return false;
    }
    return true;
}

int main()
{
    openlog(NULL, 0, LOG_USER);

    struct addrinfo hints;
    struct addrinfo *servinfo;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if(getaddrinfo(NULL, PORT, &hints, &servinfo) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed");
        closelog();
        return -1;
    }

    int sd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(sd == -1) {
        syslog(LOG_ERR, "socket open failed");
        return -1;
    }

    if(bind(sd, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
        syslog(LOG_ERR, "bind failed");
        freeaddrinfo(servinfo);
        close(sd);
        return -1;
    }
    freeaddrinfo(servinfo);

    pid_t pid = fork();
    if(pid == -1) {
        syslog(LOG_ERR, "fork failed");
        return -1;
    }
    else if(pid == 0) {
        if(!setting_signal()) {
            return -1;
        }

        if(listen(sd, 5) != 0) {
            syslog(LOG_ERR, "listen failed");
            return -1;
        }

        char data[1024];
        while(!caught_signal) {
            struct sockaddr client;
            socklen_t client_len = sizeof(struct sockaddr);
            int sockfd = accept(sd, &client, &client_len);
            if(sockfd == -1) {
                syslog(LOG_ERR, "accept failed");
                close(sockfd);
                return -1;
            }

            struct sockaddr_in *addr_in = (struct sockaddr_in *)&client;
            char* ip = inet_ntoa(addr_in->sin_addr);
            syslog(LOG_DEBUG, "Accepted connection from %s", ip);

            int fd = open(OUTPUT_FILE, O_WRONLY | O_APPEND | O_CREAT, 0666);
            if(fd == -1) {
                syslog(LOG_ERR, "file open create write failed");
                return -1;
            }

            int ret_len;
            while((ret_len = recv(sockfd, data, sizeof(data), 0)) == sizeof(data)) {
                write(fd, data, ret_len);
            }

            if(ret_len > 0) {
                write(fd, data, ret_len);
            }

            close(fd);

            fd = open(OUTPUT_FILE, O_RDONLY);
            if(fd == -1) {
                syslog(LOG_ERR, "file open read failed");
                return -1;
            }

            int read_len;
            while((read_len = read(fd, data, sizeof(data))) > 0){
                send(sockfd, data, read_len, 0);
            }
            close(fd);
        }
    }

    close(sd);

    return 0;
}