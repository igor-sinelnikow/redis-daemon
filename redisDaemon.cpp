#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <hiredis.h>

using std::equal;
using std::istream_iterator;
using std::istringstream;
using std::string;
using std::to_string;
using std::vector;

static ssize_t sendall(int sockfd, const char *buf, size_t len, int flags) {
    size_t total = 0;
    ssize_t n;

    while (total < len) {
        n = send(sockfd, buf+total, len-total, flags);
        if (n == -1) {
            return (total == 0) ? -1 : total;
        }
        total += n;
    }

    return total;
}

static bool iequals(const string &s1, const string &s2) {
    return equal(s1.begin(), s1.end(), s2.begin(), s2.end(),
                 [](char c1, char c2) {
                     return tolower(c1) == tolower(c2);
                 });
}

#define redisSafeConnect()                                 \
    c = redisConnect("127.0.0.1", 6379);                   \
    if (c == NULL || c->err) {                             \
        answer = "Redis connection error: ";               \
        if (c) {                                           \
            answer += c->errstr;                           \
            redisFree(c);                                  \
        }                                                  \
        else {                                             \
            answer += "can't allocate redis context";      \
        }                                                  \
        answer += "\n";                                    \
        sendall(fd, answer.c_str(), answer.size(), 0);     \
                                                           \
        shutdown(fd, SHUT_RDWR);                           \
        close(fd);                                         \
        return 1;                                          \
    }

#define redisSafeReply(cmd)                                \
    if (!reply) {                                          \
        answer = cmd" error: " + string(c->errstr) + "\n"; \
        redisFree(c);                                      \
        sendall(fd, answer.c_str(), answer.size(), 0);     \
        answer.clear();                                    \
        redisSafeConnect();                                \
    }

extern "C" int work(int fd) {
    string answer;

    redisContext *c;
    redisSafeConnect();

    redisReply *reply = (redisReply*)redisCommand(c, "SETNX redisDaemon:entry:counter 0");
    freeReplyObject(reply);

    answer  = "\nЯ поддерживаю следующие команды:\n";
    answer += "\tSET value - добавление данных;\n";
    answer += "\tDEL id    - удаление данных;\n";
    answer += "\tGET id    - получение данных.\n";
    answer += "\n    Команда добавления данных передаёт строку. В результате выполнения получает\n";
    answer += "  сообщение с ID, либо об ошибке.\n";
    answer += "    Команда удаления данных передаёт ID данных. Результатом будет получение\n";
    answer += "  сообщения об успехе либо об ошибке.\n";
    answer += "    Команда получения данных передаёт ID данных. Результатом выполнения будет\n";
    answer += "  сообщение, содержащее данные, либо сообщение об ошибке.\n\n";
    sendall(fd, answer.c_str(), answer.size(), 0);

    string input;
    char ch;
    bool finished = false;
    while (!finished) {
        answer.clear();
        input.clear();

        while (!finished) {
            finished = read(fd, &ch, 1) != 1;
            if (ch == '\n') {
                break;
            }
            if (ch == EOF) {
                finished = true;
                break;
            }
            if (!iscntrl(ch)) {
                input += ch;
            }
        }

        if (!finished && !input.empty()) {
            istringstream iss(input);
            vector<string> tokens{istream_iterator<string>{iss}, istream_iterator<string>{}};
            if (tokens.size() > 0) {
                if (tokens.size() < 2) {
                    answer = "Неправильный синтаксис: " + input;
                }
                else if (iequals(tokens[0], "SET")) {
                    reply = (redisReply*)redisCommand(c, "INCR redisDaemon:entry:counter");
                    const int id = reply->integer;
                    freeReplyObject(reply);

                    reply = (redisReply*)redisCommand(c, "SET redisDaemon:entry:%d %s",
                                                      id, tokens[1].c_str());
                    redisSafeReply("SET");
                    freeReplyObject(reply);
                    answer = "ID = entry:" + to_string(id);
                }
                else if (iequals(tokens[0], "DEL")) {
                    reply = (redisReply*)redisCommand(c, "DEL redisDaemon:%s", tokens[1].c_str());
                    redisSafeReply("DEL");
                    if (reply->integer == 0) {
                        answer = "Отсутствует ID = " + tokens[1];
                    }
                    else {
                        answer = "OK";
                    }
                    freeReplyObject(reply);
                }
                else if (iequals(tokens[0], "GET")) {
                    reply = (redisReply*)redisCommand(c, "GET redisDaemon:%s", tokens[1].c_str());
                    redisSafeReply("GET");
                    if (reply->type == REDIS_REPLY_NIL) {
                        answer = "Отсутствует ID = " + tokens[1];
                    }
                    else {
                        answer = reply->str;
                    }
                    freeReplyObject(reply);
                }
                else {
                    answer = "Неизвестная команда: " + input;
                }
            }
        }

        if (!finished && !answer.empty()) {
            answer += "\n";
            sendall(fd, answer.c_str(), answer.size(), 0);
        }
    }

    redisFree(c);
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return 0;
}
