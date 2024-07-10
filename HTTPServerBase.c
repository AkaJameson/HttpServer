#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFFLEN 1024  //�������Ĵ�С 
#define SERVER_PORT 1234  //�˿ں�
#define HTTP_FILENAME_LEN   256

struct doc_type
{
    char *suffix;
    char *type;
};
//�������ݶ�Ӧ MIME ����
struct doc_type file_type[] =
{
    { "html", "text/html" },
    { "ico", "image/x-icon" },
    { NULL, NULL }
};
void * threadFun(void * args);
void handle_connect(int serv_sock);
void http_parse_request_cmd(char *buf,char *file_name, char *suffix);
char *http_get_type_by_suffix(const char *suffix);

char *http_res_hdr_tmpl = "HTTP/1.1 200 OK\nServer: bianchengbang\n"
"Accept-Ranges: bytes\nContent-Length: %d\nConnection: closed\n"
"Content-Type: %s\n\n";

int main() {
    int serv_sock;
    struct sockaddr_in serv_addr;
    //�����׽���
    if ((serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("�׽��ִ���ʧ��\n");
        exit(0);
    }

    //���׽��������� IP ��ַ�� 1234 �˿ڽ��а�
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(SERVER_PORT);
    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        close(serv_sock);
        printf("bind()ִ��ʧ��\n");
        exit(0);
    }

    //���׽��ֽ��뱻������״̬
    if (listen(serv_sock, SOMAXCONN) == -1) {
        close(serv_sock);
        printf("listen()ִ��ʧ��\n");
        exit(0);
    }

    handle_connect(serv_sock);
    close(serv_sock);
    return 0;
}

void handle_connect(int serv_sock){
    int clnt_sock;
    struct sockaddr_in clnt_addr;
    socklen_t clnt_addr_size = sizeof(clnt_addr);
    //��ѭ�����������ϵؽ�������ͻ��˷���������
    while(1){
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);//���տͻ��˷���������
        //��������һ���̣߳�����ͻ��˵�����
        if(clnt_sock >0){
            pthread_t myThread;
            int ret;
            //threadFun() Ϊ�߳�Ҫִ�еĺ�����clnt_sock ����Ϊʵ�δ��ݸ� threadFun() ����
            ret = pthread_create(&myThread, NULL, threadFun, &clnt_sock);
            if (ret != 0) {
                printf("�̴߳���ʧ��\n");
                exit(0);
            }
        }
    }
}

void * threadFun(void * args) {
    //�����̺߳����߳����룬���߳�ִ�н������Զ��ͷ���Դ
    pthread_detach(pthread_self());
    int clnt_sock = *(int*)args;
    char buff[BUFFLEN]={0};
    //��ȡhttp������ַ�����num Ϊ�ַ����ĳ���
    int  num = read(clnt_sock, buff, sizeof(buff));
    if (num > 0) {
        FILE * fp = NULL;
        int nCount = 0;
        int fp_has = 1,fp_type = 1;
        int file_len, hdr_len;
        char *type = NULL;
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
            fp = fopen("errno.html","rb");
        }else{
            fp = fopen(file_name, "rb");
            //���������δ�ҵ�Ŀ���ļ�����ͻ��˷��� errno.html �ļ�
            if (fp == NULL) {
                fp_has = 0;
                fp = fopen("errno.html","rb");
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

        if(fp_type == 1){
            if (fp_has == 0) {
                printf("������������ %s �ļ�\n", file_name);
            }
            else {
                printf("���������� %s �ļ���������...\n",file_name);
            }
        }
        //��ͻ��˷����ļ����ݣ�����Ӧ��
        memset(buff, 0, BUFFLEN);
        while ((nCount = fread(buff, 1, BUFFLEN, fp)) > 0) {
            write(clnt_sock, buff, nCount);
            memset(buff, 0, BUFFLEN);
        }
        fclose(fp);
        shutdown(clnt_sock, SHUT_WR);
        read(clnt_sock, buff, sizeof(buff));
        close(clnt_sock);
    }
    return NULL;
}

char *http_get_type_by_suffix(const char *suffix)
{
    struct doc_type *type = NULL;

    for (type = file_type; type->suffix; type++)
    {
        if (strcmp(type->suffix, suffix) == 0)
            return type->type;
    }

    return NULL;
}

void http_parse_request_cmd(char *buf,char *file_name, char *suffix)
{
    int file_length = 0, suffix_length = 0;
    char *begin=NULL, *end=NULL, *bias=NULL;

    //���� URL �Ŀ�ʼλ��
    begin = strchr(buf, ' ');
    begin += 1;

    //���� URL �Ľ���λ��
    end = strchr(begin, ' ');
    *end = 0;
    //�õ�Ҫ���ʵ�Ŀ���ļ�����·����
    file_length = end - begin - 1;
    memcpy(file_name, begin+1, file_length);
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
