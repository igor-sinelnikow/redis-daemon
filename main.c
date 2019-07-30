#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

int work(int fd);

#define LISTEN_BACKLOG 64

void usage(const char *program) {
    fprintf(stderr, "Usage: %s <address:port>\n", program);
    fprintf(stderr, "    Демон, позволяющий работать с данными, хранящимися на redis сервере.");
    fprintf(stderr, " Демон\n  поддерживает команды добавления, удаления и получения данных.\n");
    fprintf(stderr, "    Команды передаются при помощи socket-ов. Адрес работы сокетов демон");
    fprintf(stderr, " получает\n  на входе при старте в формате <address:port>\n");
    exit(EXIT_FAILURE);
}

void sighandler(int signum) {
    waitpid(0, 0, WNOHANG);
}

int main(int argc, char const *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
    }

    size_t addrlen = strcspn(argv[1], ":");
    if (strlen(argv[1]) - addrlen <= 1) {
        usage(argv[0]);
    }

    addrlen += 1;
    char addr[addrlen];
    uint port;
    const int parsed = sscanf(argv[1], "%[^:]:%u", addr, &port);
    if (parsed != 2) {
        usage(argv[0]);
    }

    struct hostent *host = gethostbyname(addr);
    if (!host) {
        herror("gethostbyname");
        return 1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 2;
    }
    if (pid) {
        printf("PID=%d\n", pid);
        return 0;
    }

    umask(0);

    if (setsid() == -1) {
        perror("setsid");
        return 3;
    }
    if (chdir("/") == -1) {
        perror("chdir");
        return 4;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &sighandler;
    sigaction(SIGCHLD, &sa, 0);

    struct rlimit rlim;
    getrlimit(RLIMIT_NOFILE, &rlim);
    rlim.rlim_cur *= 64;
    rlim.rlim_max *= 64;
    setrlimit(RLIMIT_NOFILE, &rlim);

    pid = getpid();
    char ident[24];
    snprintf(ident, 24, "redis-daemon[%d]", pid);
    openlog(ident, 0, LOG_USER);

    int listener = socket(host->h_addrtype, SOCK_STREAM, 0);
    if (listener == -1) {
        syslog(LOG_ERR, "socket: %s", strerror(errno));
        return 5;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = host->h_addrtype;
    sin.sin_port = htons(port);
    memcpy(&sin.sin_addr.s_addr, host->h_addr, host->h_length);
    if (bind(listener, (struct sockaddr*) &sin, sizeof(sin)) == -1) {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        return 6;
    }

    if (listen(listener, LISTEN_BACKLOG) == -1) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        return 7;
    }

    socklen_t sin_size;
    int sockfd;
    for (;;) {
        sin_size = sizeof(sin);
        if ((sockfd = accept(listener, (struct sockaddr*) &sin, &sin_size)) == -1) {
            continue;
        }
        syslog(LOG_NOTICE, "connection from %s:%d", inet_ntoa(sin.sin_addr),
               ntohs(sin.sin_port));

        switch (fork()) {
        case -1:
            syslog(LOG_ERR, "fork: %s", strerror(errno));
            break;
        case 0:
            close(listener);
            return work(sockfd);
        default:
            close(sockfd);
            break;
        }
    }

    closelog();
    return 0;
}
