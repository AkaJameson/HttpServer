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

#define BUFFLEN 1024  //�������Ĵ�С
#define SERVER_PORT 1234  //�˿ں�
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
    //�趨���� serv_sock �ķ�ʽ
    struct epoll_event event;
    //�趨���ӷ�ʽΪ EPOLLIN
    event.events = events;
    //���� serv_sock �ļ���������ֵ
    event.data.fd = fd;
    //�� serv_sock ��ӵ� epfd ��
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
}

void http_parse_request_cmd(char* buf, char* file_name, char* suffix)
{
    int file_length = 0, suffix_length = 0;
    char* begin = NULL, * end = NULL, * bias = NULL;
    //���� URL �Ŀ�ʼλ��
    begin = strchr(buf, ' ');
    begin += 1;
    //���� URL �Ľ���λ��
    end = strchr(begin, ' ');
    *end = 0;
    //�õ�Ҫ���ʵ�Ŀ���ļ�����·����
    file_length = end - begin - 1;
    memcpy(file_name, begin + 1, file_length);
    file_name[file_length] = 0;
    //����ļ��ĺ�׺��
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
    //�����׽���
    int serv_sock;
    if ((serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        error_die("�׽��ִ���ʧ��:");
    }

    struct sockaddr_in serv_addr;
    //���׽��������� IP ��ַ�� 1234 �˿ڽ��а�
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(SERVER_PORT);

    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        error_die("bind()ִ��ʧ��:");
        close(serv_sock);
    }

    //���׽��ֽ��뱻������״̬
    if (listen(serv_sock, SOMAXCONN) == -1) {
        error_die("listen()ִ��ʧ��:");
        close(serv_sock);
    }
    return serv_sock;
}

void* threadFun(void* args) {
    //�����̺߳����߳����룬���߳�ִ�н������Զ��ͷ���Դ
    pthread_detach(pthread_self());
    info* fds = (info*)args;
    int epfd = fds->epollfd;
    int clnt_sock = fds->fd;
    free(fds);
    char buff[BUFFLEN] = { 0 };
    while (1) {
        memset(buff, 0, BUFFLEN);
        //��ȡ���ӵ����ļ�������������
        int str_len = read(clnt_sock, buff, BUFFLEN);
        if (str_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("ѭ��������������\n");
            }
            close(clnt_sock);
            break;
        }
        //�����ȡ����Ϊ 0����ʾ�ͻ�����ͷ������˶Ͽ�
        else if (str_len == 0) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, clnt_sock, NULL);
            close(clnt_sock);
            printf("�Ϳͻ��� %d �Ͽ�����\n", clnt_sock);
        }
        //����ͻ��˵����󣬽��������Դ���͸��ͻ���
        else {
            FILE* fp = NULL;
            int nCount = 0;
            int fp_has = 1, fp_type = 1;
            int file_len, hdr_len;
            char* type = NULL;
            char http_header[BUFFLEN];
            char file_name[HTTP_FILENAME_LEN] = { 0 }, suffix[16] = { 0 };
            //��ȡĿ���ļ�����·�����ͺ�׺��
            http_parse_request_cmd(buff, file_name, suffix);
            //��ȡ�ļ���Ӧ�� MIME ����
            type = http_get_type_by_suffix(suffix);
            //�������δ�ҵ�������ͻ��˷��� errno.html �ļ�
            if (type == NULL)
            {
                fp_type = 0;
                printf("���ʵ��ļ����ͣ���׺������ƥ��\n");
                type = http_get_type_by_suffix("html");
                fp = fopen("errno.html", "rb");
            }
            else {
                fp = fopen(file_name, "rb");
                //���������δ�ҵ�Ŀ���ļ�����ͻ��˷��� errno.html �ļ�
                if (fp == NULL) {
                    fp_has = 0;
                    fp = fopen("errno.html", "rb");
                }
            }
            //�����ļ��а������ֽ���
            fseek(fp, 0, SEEK_END);
            file_len = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            //���� http ��Ӧ���ַ���
            hdr_len = sprintf(http_header, http_res_hdr_tmpl, file_len, type);
            //��ͻ��˷�����Ӧ�С���Ӧͷ�Ϳ���
            write(clnt_sock, http_header, hdr_len);
            if (fp_type == 1) {
                if (fp_has == 0) {
                    printf("----������������ %s �ļ�,errno.html �ļ�������...\n", file_name);
                    strcpy(file_name, "errno.html");
                }
                else {
                    printf("----���������� %s �ļ���������...\n", file_name);
                }
            }
            //��ͻ��˷����ļ����ݣ�����Ӧ��
            memset(buff, 0, BUFFLEN);
            while ((nCount = fread(buff, 1, BUFFLEN, fp)) > 0) {
                write(clnt_sock, buff, nCount);
                memset(buff, 0, BUFFLEN);
            }
            printf("----�ļ� %s �������\n", file_name);
            fclose(fp);
            shutdown(clnt_sock, SHUT_WR);
            read(clnt_sock, buff, sizeof(buff));
            epoll_ctl(epfd, EPOLL_CTL_DEL, clnt_sock, NULL);
            close(clnt_sock);
            printf("�����Ϳͻ���%d�Ͽ�����\n", clnt_sock);
        }
    }
    return NULL;
}

void handle_connect(int serv_sock) {
    //���ں˿ռ��������ڴ棬׼������ļ�������
    int epfd = epoll_create(1);
    //�������������������ķ�ʽ���� serv_sock
    addfd(epfd, serv_sock, EPOLLIN);
    //Ϊ���� epoll_wait ���ӵ����ļ���������׼��
    struct epoll_event* ep_events = malloc(sizeof(struct epoll_event) * EPOLL_SIZE);
    while (1) {
        //��⵽ epfd �ڵ��ļ������������仯�����߼���ʱ�䳬�� 10 �룬�����ͷ���
        int event_cnt = epoll_wait(epfd, ep_events, EPOLL_SIZE, 10000);
        //�ж� epoll_wait() �Ƿ�ִ��ʧ��
        if (event_cnt == -1) {
            perror("epoll_wait() ����ʧ��:");
            break;
        }
        if (event_cnt == 0) {
            printf("----event_cnt���ڼ�����\n");
            continue;
        }
        //����������ӵ��������ļ�������
        for (int i = 0; i < event_cnt; i++) {
            if (ep_events[i].data.fd == serv_sock) {
                struct sockaddr_in clnt_addr;
                socklen_t adr_sz = sizeof(clnt_addr);
                int clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &adr_sz);
                //���� clnt_sock Ϊ������ģʽ
                fcntl(clnt_sock, F_SETFL, O_NONBLOCK);
                //�������ӵĿͻ��˵��ļ���������ӵ� epfd ��
                addfd(epfd, clnt_sock, EPOLLIN | EPOLLET | EPOLLONESHOT);
                printf("�ͻ���%d���ӳɹ�\n", clnt_sock);
            }
            else {
                info* fds = (info*)malloc(sizeof(info));
                fds->epollfd = epfd;
                fds->fd = ep_events[i].data.fd;
                pthread_t myThread;
                //threadFun() Ϊ�߳�Ҫִ�еĺ�����clnt_sock ����Ϊʵ�δ��ݸ� threadFun() ����
                int ret = pthread_create(&myThread, NULL, threadFun, (void*)fds);
                if (ret != 0) {
                    printf("�̴߳���ʧ��\n");
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
