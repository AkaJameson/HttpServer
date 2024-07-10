#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFFLEN 1024  //缓冲区的大小 
#define SERVER_PORT 1234  //端口号
#define HTTP_FILENAME_LEN   256

struct doc_type
{
    char *suffix;
    char *type;
};
//返回内容对应 MIME 类型
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
    //创建套接字
    if ((serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        printf("套接字创建失败\n");
        exit(0);
    }

    //将套接字与任意 IP 地址和 1234 端口进行绑定
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(SERVER_PORT);
    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        close(serv_sock);
        printf("bind()执行失败\n");
        exit(0);
    }

    //让套接字进入被动监听状态
    if (listen(serv_sock, SOMAXCONN) == -1) {
        close(serv_sock);
        printf("listen()执行失败\n");
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
    //死循环，持续不断地接收任意客户端发来的请求
    while(1){
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);//接收客户端发来的请求
        //单独创建一个线程，处理客户端的请求
        if(clnt_sock >0){
            pthread_t myThread;
            int ret;
            //threadFun() 为线程要执行的函数，clnt_sock 将作为实参传递给 threadFun() 函数
            ret = pthread_create(&myThread, NULL, threadFun, &clnt_sock);
            if (ret != 0) {
                printf("线程创建失败\n");
                exit(0);
            }
        }
    }
}

void * threadFun(void * args) {
    //将子线程和主线程脱离，子线程执行结束后自动释放资源
    pthread_detach(pthread_self());
    int clnt_sock = *(int*)args;
    char buff[BUFFLEN]={0};
    //获取http请求的字符串，num 为字符串的长度
    int  num = read(clnt_sock, buff, sizeof(buff));
    if (num > 0) {
        FILE * fp = NULL;
        int nCount = 0;
        int fp_has = 1,fp_type = 1;
        int file_len, hdr_len;
        char *type = NULL;
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
            fp = fopen("errno.html","rb");
        }else{
            fp = fopen(file_name, "rb");
            //如果服务器未找到目标文件，向客户端发送 errno.html 文件
            if (fp == NULL) {
                fp_has = 0;
                fp = fopen("errno.html","rb");
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

        if(fp_type == 1){
            if (fp_has == 0) {
                printf("服务器不存在 %s 文件\n", file_name);
            }
            else {
                printf("服务器存在 %s 文件，发送中...\n",file_name);
            }
        }
        //向客户端发送文件内容，即响应体
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

    //查找 URL 的开始位置
    begin = strchr(buf, ' ');
    begin += 1;

    //查找 URL 的结束位置
    end = strchr(begin, ' ');
    *end = 0;
    //得到要访问的目标文件（含路径）
    file_length = end - begin - 1;
    memcpy(file_name, begin+1, file_length);
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
