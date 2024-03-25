#include <stdio.h>
#include <stdlib.h>
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
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>


#define USE_AESD_CHAR_DEVICE 1

#if (USE_AESD_CHAR_DEVICE == 1)
#include <sys/ioctl.h>
#define OUTPUT_FILE        "/dev/aesdchar"
#define AESD_IOC_MAGIC 0x16
#define AESDCHAR_IOCSEEKTO _IOWR(AESD_IOC_MAGIC, 1, struct aesd_seekto)
#else
#define OUTPUT_FILE        "/var/tmp/aesdsocketdata"
#endif

#define PORT               "9000"
#define BACKLOG            10
#define BUF_SIZE           1024
#define TIMESTAMP_INTERVAL 10

struct timestamp_data {
    pthread_mutex_t *mutex;
};

struct thread_data {
    int sockfd;
    pthread_mutex_t *mutex;
    bool complete;
};

struct thread_node {
    pthread_t thread;
    struct thread_data *data;
    LIST_ENTRY(thread_node) node;
};

struct aesd_seekto {
    /**
     * The zero referenced write command to seek into
     */
    uint32_t write_cmd;
    /**
     * The zero referenced offset within the write
     */
    uint32_t write_cmd_offset;
};

LIST_HEAD(listhead, thread_node) head;
bool caught_signal = false;

static void signal_handler(int signal_number)
{
    if(signal_number == SIGINT || signal_number == SIGTERM) {
        caught_signal = true;
        syslog(LOG_INFO, "Caught signal, exiting");
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

void* data_handler(void* thread_param)
{
    struct thread_data* data = (struct thread_data *) thread_param;
    char buf[BUF_SIZE];

    int rc = pthread_mutex_lock(data->mutex);
    if(rc != 0) {
        printf("lock mutex error %d\n", rc);
        data->complete = true;
        return thread_param;
    }

    int wd = open(OUTPUT_FILE, O_WRONLY | O_APPEND | O_CREAT, 0666);
    if(wd == -1) {
        syslog(LOG_ERR, "file open create write failed");
        return thread_param;
    }
    int ret_len;
    struct aesd_seekto seekto;
    bool found = false;
    while((ret_len = recv(data->sockfd, buf, BUF_SIZE, 0)) > 0) {
#if (USE_AESD_CHAR_DEVICE == 1)
        if(sscanf(buf, "AESDCHAR_IOCSEEKTO:%d,%d", &seekto.write_cmd, &seekto.write_cmd_offset) == 2) {
            found = true;
            break;
        } else {
#endif
            int len = write(wd, buf, ret_len);
            if(len == -1) {
                syslog(LOG_ERR, "write file failed");
                close(wd);
                data->complete = true;
                return thread_param;
            }
            if(buf[ret_len-1]=='\n') {
                break;
            }
#if (USE_AESD_CHAR_DEVICE == 1)
        }
#endif
    }
    close(wd);

    int rd = open(OUTPUT_FILE, O_RDONLY);
    if(rd == -1) {
        syslog(LOG_ERR, "file open read failed");
        data->complete = true;
        return thread_param;
    }

#if (USE_AESD_CHAR_DEVICE == 1)
    if(found) {
        ioctl(rd, AESDCHAR_IOCSEEKTO, &seekto);
    }
#endif

    while((ret_len = read(rd, buf, BUF_SIZE)) > 0){
        send(data->sockfd, buf, ret_len, 0);
    }
    close(rd);

    rc = pthread_mutex_unlock(data->mutex);
    if(rc != 0){
        printf("mutex unlock error %d\n", rc);
    }

    data->complete = true;
    return thread_param;
}

void* timestamp_handler(void* thread_param)
{
    struct timestamp_data* data = (struct timestamp_data *)thread_param;
    char buffer[100];
    char str[BUF_SIZE];
    struct timespec ts;

    ts.tv_sec = TIMESTAMP_INTERVAL;
    ts.tv_nsec = 0;

    while(!caught_signal) {
        int rc = clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
        if(rc != 0){
            printf("clock_nanosleep error %d\n", rc);
            return thread_param;
        }
        time_t now;
        struct tm *tm_info;
        time(&now);
        tm_info = localtime(&now);
        strftime(buffer, sizeof(buffer), "%a, %d %b %Y %T %z", tm_info);
        sprintf(str, "timestamp:%s\n", buffer);

        rc = pthread_mutex_lock(data->mutex);
        if(rc != 0) {
            printf("lock mutex error %d\n", rc);
            return thread_param;
        }

        int wd = open(OUTPUT_FILE, O_WRONLY | O_APPEND | O_CREAT, 0666);
        if(wd == -1) {
            syslog(LOG_ERR, "file open create write failed");
            return thread_param;
        }
        
        int len = write(wd, str, strlen(str));
        if(len == -1) {
            syslog(LOG_ERR, "file open create write failed");
        }
        close(wd);

        rc = pthread_mutex_unlock(data->mutex);
        if(rc != 0){
            printf("mutex unlock error %d\n", rc);
            return thread_param;
        }
    }
    return thread_param;
}

int main(int argc, char* argv[])
{
    openlog(NULL, 0, LOG_USER);

    if(!setting_signal()) {
        goto err1;
    }

    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if(getaddrinfo(NULL, PORT, &hints, &servinfo) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed");
        goto err1;
    }

    int sd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(sd == -1) {
        syslog(LOG_ERR, "socket open failed");
        goto err1;
    }

    if(setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
        syslog(LOG_ERR, "set socket option failed");
        goto err2;
    }

    int ret = bind(sd, servinfo->ai_addr, servinfo->ai_addrlen);
    if(ret != 0) {
        syslog(LOG_ERR, "bind failed");
        goto err2;
    }
    freeaddrinfo(servinfo);

    pid_t pid;
    if(argc>1 && strcmp(argv[1], "d")) {
        switch(pid = fork()) {
            case -1:
                syslog(LOG_ERR, "fork failed");
                goto err2;

            case 0:
                break;

            default:
                return 0;
        }
    }

    if(listen(sd, BACKLOG) != 0) {
        syslog(LOG_ERR, "listen failed");
        goto err2;
    }

    struct listhead list_head = LIST_HEAD_INITIALIZER(head);
    LIST_INIT(&list_head);
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
#if (USE_AESD_CHAR_DEVICE == 0)
    struct timestamp_data time_data;
    time_data.mutex = &mutex;
    pthread_t timestamp_thread;
    ret = pthread_create(&timestamp_thread, NULL, timestamp_handler, &time_data);
    if(ret != 0) {
        printf("error pthread_create for timestamp\n");
        goto err3;
    }
#endif

    struct thread_node *cur, *next;
    while(!caught_signal) {
        struct sockaddr client;
        socklen_t client_len = sizeof(struct sockaddr);
        int sockfd = accept(sd, &client, &client_len);
        if(sockfd == -1) {
            syslog(LOG_ERR, "accept failed");
            continue;
        }

        struct sockaddr_in *addr_in = (struct sockaddr_in *)&client;
        char* ip = inet_ntoa(addr_in->sin_addr);
        syslog(LOG_DEBUG, "Accepted connection from %s", ip);

        struct thread_data* data = malloc(sizeof(struct thread_data));
        data->sockfd = sockfd;
        data->mutex = &mutex;
        data->complete = false;
        struct thread_node* t = malloc(sizeof(struct thread_node));
        t->data = data;
        int rc = pthread_create(&t->thread, NULL, data_handler, data);
        if(rc != 0) {
            printf("error pthread_create\n");
            close(sockfd);
            break;
        }
        LIST_INSERT_HEAD(&list_head, t, node);

        for(cur=LIST_FIRST(&list_head);cur!=NULL;cur=next) {
            next = LIST_NEXT(cur, node);
            if(cur->data->complete) {
                pthread_join(cur->thread, NULL);
                close(cur->data->sockfd);
                free(cur->data);
                LIST_REMOVE(cur, node);
                free(cur);
            }
        }
    }

    
    for(cur=LIST_FIRST(&list_head);cur!=NULL;cur=next) {
        next = LIST_NEXT(cur, node);
        pthread_cancel(cur->thread);
        pthread_join(cur->thread, NULL);
        close(cur->data->sockfd);
        free(cur->data);
        free(cur);
    }

#if (USE_AESD_CHAR_DEVICE == 0)
    pthread_cancel(timestamp_thread);
    pthread_join(timestamp_thread, NULL);
#endif
    
    pthread_mutex_destroy(&mutex);
    close(sd);
    closelog();
#if (USE_AESD_CHAR_DEVICE == 0)
    remove(OUTPUT_FILE);
#endif

    return 0;

#if (USE_AESD_CHAR_DEVICE == 0)
err3:
    pthread_mutex_destroy(&mutex);
#endif
err2:
    close(sd);
err1:
    freeaddrinfo(servinfo);
    closelog();
#if (USE_AESD_CHAR_DEVICE == 0)
    remove(OUTPUT_FILE);
#endif

    return -1;
}