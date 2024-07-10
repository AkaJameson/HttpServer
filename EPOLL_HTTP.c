#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFLEN 1024  //缓冲区的大小
#define SERVER_PORT 1234  //端口号
#define HTTP_FILENAME_LEN   256
#define EPOLL_SIZE 1000

char* http_res_hdr_tmpl = "HTTP/1.1 200 OK\nServer: bianchengbang\n"
"Accept-Ranges: bytes\nContent-Length: %d\nConnection: closed\n"
"Content-Type: %s\n\n";

typedef struct info {
    int epollfd;
    int fd;
}info;

struct doc_type
{
    char* suffix;
    char* type;
}file_type[] ={
    { "html", "text/html" },
    { "ico", "image/x-icon" },
    { "png", "image/png"},
    {"js","application/x-javascript"},
    {"css","text/css"},
    {"jpg","image/jpeg"},
    {"gif","text/html"}
};

void error_die(const char* str) {
    perror(str);
    exit(1);
}

void addfd(int epfd, int fd, int events) {
    //设定监视 serv_sock 的方式
    struct epoll_event event;
    //设定监视方式为 EPOLLIN
    event.events = events;
    //保存 serv_sock 文件描述符的值
    event.data.fd = fd;
    //将 serv_sock 添加到 epfd 中
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
}

void http_parse_request_cmd(char* buf, char* file_name, char* suffix)
{
    int file_length = 0, suffix_length = 0;
    char* begin = NULL, * end = NULL, * bias = NULL;
    //查找 URL 的开始位置
    begin = strchr(buf, ' ');
    begin += 1;
    //查找 URL 的结束位置
    end = strchr(begin, ' ');
    *end = 0;
    //得到要访问的目标文件（含路径）
    file_length = end - begin - 1;
    memcpy(file_name, begin + 1, file_length);
    file_name[file_length] = 0;
    //获得文件的后缀名
    bias = strrchr(begin, '/');
    suffix_length = end - bias;
    if (*bias == '/')
    {
        bias++;
        suffix_length--;
    }
    if (suffix_length > 0)
    {
        begin = strchr(file_name, '.');
        if (begin)
            strcpy(suffix, begin + 1);
    }
}

char* http_get_type_by_suffix(const char* suffix)
{
    struct doc_type* type = NULL;
    for (type = file_type; type->suffix; type++)
    {
        if (strcmp(type->suffix, suffix) == 0)
            return type->type;
    }
    return NULL;
}

int serv_init() {
    //创建套接字
    int serv_sock;
    if ((serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        error_die("套接字创建失败:");
    }

    struct sockaddr_in serv_addr;
    //将套接字与任意 IP 地址和 1234 端口进行绑定
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(SERVER_PORT);

    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        error_die("bind()执行失败:");
        close(serv_sock);
    }

    //让套接字进入被动监听状态
    if (listen(serv_sock, SOMAXCONN) == -1) {
        error_die("listen()执行失败:");
        close(serv_sock);
    }
    return serv_sock;
}

void* threadFun(void* args) {
    //将子线程和主线程脱离，子线程执行结束后自动释放资源
    pthread_detach(pthread_self());
    info* fds = (info*)args;
    int epfd = fds->epollfd;
    int clnt_sock = fds->fd;
    free(fds);
    char buff[BUFFLEN] = { 0 };
    while (1) {
        memset(buff, 0, BUFFLEN);
        //读取监视到的文件描述符的数据
        int str_len = read(clnt_sock, buff, BUFFLEN);
        if (str_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("循环读完所有数据\n");
            }
            close(clnt_sock);
            break;
        }
        //如果读取长度为 0，表示客户端想和服务器端断开
        else if (str_len == 0) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, clnt_sock, NULL);
            close(clnt_sock);
            printf("和客户端 %d 断开连接\n", clnt_sock);
        }
        //处理客户端的请求，将请求的资源发送给客户端
        else {
            FILE* fp = NULL;
            int nCount = 0;
            int fp_has = 1, fp_type = 1;
            int file_len, hdr_len;
            char* type = NULL;
            char http_header[BUFFLEN];
            char file_name[HTTP_FILENAME_LEN] = { 0 }, suffix[16] = { 0 };
            //获取目标文件（含路径）和后缀名
            http_parse_request_cmd(buff, file_name, suffix);
            //获取文件对应的 MIME 类型
            type = http_get_type_by_suffix(suffix);
            //如果类型未找到，则向客户端发送 errno.html 文件
            if (type == NULL)
            {
                fp_type = 0;
                printf("访问的文件类型（后缀名）不匹配\n");
                type = http_get_type_by_suffix("html");
                fp = fopen("errno.html", "rb");
            }
            else {
                fp = fopen(file_name, "rb");
                //如果服务器未找到目标文件，向客户端发送 errno.html 文件
                if (fp == NULL) {
                    fp_has = 0;
                    fp = fopen("errno.html", "rb");
                }
            }
            //计算文件中包含的字节数
            fseek(fp, 0, SEEK_END);
            file_len = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            //更新 http 响应的字符串
            hdr_len = sprintf(http_header, http_res_hdr_tmpl, file_len, type);
            //向客户端发送响应行、响应头和空行
            write(clnt_sock, http_header, hdr_len);
            if (fp_type == 1) {
                if (fp_has == 0) {
                    printf("----服务器不存在 %s 文件,errno.html 文件发送中...\n", file_name);
                    strcpy(file_name, "errno.html");
                }
                else {
                    printf("----服务器存在 %s 文件，发送中...\n", file_name);
                }
            }
            //向客户端发送文件内容，即响应体
            memset(buff, 0, BUFFLEN);
            while ((nCount = fread(buff, 1, BUFFLEN, fp)) > 0) {
                write(clnt_sock, buff, nCount);
                memset(buff, 0, BUFFLEN);
            }
            printf("----文件 %s 发送完毕\n", file_name);
            fclose(fp);
            shutdown(clnt_sock, SHUT_WR);
            read(clnt_sock, buff, sizeof(buff));
            epoll_ctl(epfd, EPOLL_CTL_DEL, clnt_sock, NULL);
            close(clnt_sock);
            printf("主动和客户端%d断开连接\n", clnt_sock);
        }
    }
    return NULL;
}

void handle_connect(int serv_sock) {
    //在内核空间中申请内存，准备存放文件描述符
    int epfd = epoll_create(1);
    //以条件触发（阻塞）的方式监听 serv_sock
    addfd(epfd, serv_sock, EPOLLIN);
    //为接收 epoll_wait 监视到的文件描述符做准备
    struct epoll_event* ep_events = malloc(sizeof(struct epoll_event) * EPOLL_SIZE);
    while (1) {
        //检测到 epfd 内的文件描述符发生变化，或者监听时间超过 10 秒，函数就返回
        int event_cnt = epoll_wait(epfd, ep_events, EPOLL_SIZE, 10000);
        //判断 epoll_wait() 是否执行失败
        if (event_cnt == -1) {
            perror("epoll_wait() 运行失败:");
            break;
        }
        if (event_cnt == 0) {
            printf("----event_cnt正在监听中\n");
            continue;
        }
        //逐个遍历监视到的所有文件描述符
        for (int i = 0; i < event_cnt; i++) {
            if (ep_events[i].data.fd == serv_sock) {
                struct sockaddr_in clnt_addr;
                socklen_t adr_sz = sizeof(clnt_addr);
                int clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &adr_sz);
                //设置 clnt_sock 为非阻塞模式
                fcntl(clnt_sock, F_SETFL, O_NONBLOCK);
                //将新连接的客户端的文件描述符添加到 epfd 中
                addfd(epfd, clnt_sock, EPOLLIN | EPOLLET | EPOLLONESHOT);
                printf("客户端%d连接成功\n", clnt_sock);
            }
            else {
                info* fds = (info*)malloc(sizeof(info));
                fds->epollfd = epfd;
                fds->fd = ep_events[i].data.fd;
                pthread_t myThread;
                //threadFun() 为线程要执行的函数，clnt_sock 将作为实参传递给 threadFun() 函数
                int ret = pthread_create(&myThread, NULL, threadFun, (void*)fds);
                if (ret != 0) {
                    printf("线程创建失败\n");
                    exit(0);
                }
            }
        }
    }
    free(ep_events);
}

int main() {
    int serv_sock = serv_init();
    handle_connect(serv_sock);
    close(serv_sock);
    return 0;
}
