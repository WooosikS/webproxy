#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestline_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio);
int connect_endServer(char *hostname,int port,char *http_header);

int main(int argc,char **argv)
{
    int listenfd,connfd;
    socklen_t  clientlen;       // 길이가 있어야 버퍼에다가 길이만큼 버퍼에 넣을 수 있다.
    char hostname[MAXLINE],port[MAXLINE];   // hostname, port 다 client 꺼

    struct sockaddr_storage clientaddr;/*generic sockaddr struct which is 28 Bytes.The same use as sockaddr*/

    if(argc != 2){
        fprintf(stderr,"usage :%s <port> \n",argv[0]);      // stderr 는 버퍼 없이 바로 출력 가능
        exit(1);        
    }
                                                            // fprintf는 stderr에다가 에러 담아서 출력하고 싶을 때 써야된다

    listenfd = Open_listenfd(argv[1]);          // listenfd = 파일 듣기 소켓 식별자 
    while(1){
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd,(SA *)&clientaddr,&clientlen);     // Accept, accept 차이점 오류가 났을 때 무슨 오류인지 print( Accept 가)

        /*print accepted message*/
        Getnameinfo((SA*)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);    // Accept 하면서 가져와서 getnameinfo 넣는다
        printf("Accepted connection from (%s %s).\n",hostname,port);            // client 꺼

        /*sequential handle the client transaction*/
        doit(connfd);           // connect 성공 했을 때

        Close(connfd);
    }
    return 0;
}

/*handle the client HTTP transaction*/
void doit(int connfd)
{
    int end_serverfd;/*the end server file descriptor*/         // tiny server

    char buf[MAXLINE],method[MAXLINE],uri[MAXLINE],version[MAXLINE];
    char endserver_http_header [MAXLINE];
    /*store the request line arguments*/
    char hostname[MAXLINE],path[MAXLINE];
    int port;

    rio_t rio,server_rio;/*rio is client's rio,server_rio is endserver's rio*/

    Rio_readinitb(&rio,connfd);         // connect fd 에 rio 연결   버퍼에 담은 것
    Rio_readlineb(&rio,buf,MAXLINE);
    sscanf(buf,"%s %s %s",method,uri,version); // 버퍼에서 빼온것을 method, uri, version 에 넣는 것. 매핑

    if(strcasecmp(method,"GET")){           // 대문자 상관없이  받는다.
        printf("Proxy does not implement the method");
        return;
    }
    /*parse the uri to get hostname,file path ,port*/
    parse_uri(uri,hostname,path,&port);

    /*build the http header which will send to the end server*/
    build_http_header(endserver_http_header,hostname,path,port,&rio);           
                                                        // 프록시도 엔드서버에 요청을 보내야 되는데
                                                          // 프록시가 엔드서버에 요청할 리퀘스트 헤더
                                                        // path 는 / 파일 이름이다.

    /*connect to the end server*/
    end_serverfd = connect_endServer(hostname,port,endserver_http_header);
    if(end_serverfd<0){
        printf("connection failed\n");
        return;
    }

    Rio_readinitb(&server_rio,end_serverfd);            // end serverfd 5번
    /*write the http header to endserver*/
    Rio_writen(end_serverfd,endserver_http_header,strlen(endserver_http_header));

    /*receive message from end server and send to the client*/
    size_t n;
    while((n=Rio_readlineb(&server_rio,buf,MAXLINE))!=0)
    {
        printf("proxy received %d bytes,then send\n",n);
        Rio_writen(connfd,buf,n);           // 프록시가 서버로 보낸거 서버가 다시 프록시로 보내는 과정.
    }
    Close(end_serverfd);
}

void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio)
{
    char buf[MAXLINE],request_hdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];
    /*request line*/
    sprintf(request_hdr,requestline_hdr_format,path);
    /*get other request header for client rio and change it */
    while(Rio_readlineb(client_rio,buf,MAXLINE)>0)
    {
        if(strcmp(buf,endof_hdr)==0) break;/*EOF*/

        if(!strncasecmp(buf,host_key,strlen(host_key)))/*Host:*/
        {
            strcpy(host_hdr,buf);
            continue;
        }

        if(strncasecmp(buf,connection_key,strlen(connection_key))
                &&strncasecmp(buf,proxy_connection_key,strlen(proxy_connection_key))
                &&strncasecmp(buf,user_agent_key,strlen(user_agent_key)))
        {
            strcat(other_hdr,buf);
        }
    }
    if(strlen(host_hdr)==0)
    {
        sprintf(host_hdr,host_hdr_format,hostname);
    }
    sprintf(http_header,"%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            prox_hdr,
            user_agent_hdr,
            other_hdr,
            endof_hdr);

    return ;
}
/*Connect to the end server*/
inline int connect_endServer(char *hostname,int port,char *http_header){
    char portStr[100];
    sprintf(portStr,"%d",port);
    return Open_clientfd(hostname,portStr);
}

/*parse the uri to get hostname,file path ,port*/
void parse_uri(char *uri,char *hostname,char *path,int *port)
{
    *port = 80;                     // port 번호 안 달아줘도 자동으로 80으로 해준다.
    char* pos = strstr(uri,"//");

    pos = pos!=NULL? pos+2:uri;

    char*pos2 = strstr(pos,":");
    if(pos2!=NULL)
    {
        *pos2 = '\0';
        sscanf(pos,"%s",hostname);
        sscanf(pos2+1,"%d%s",port,path);
    }
    else
    {
        pos2 = strstr(pos,"/");
        if(pos2!=NULL)
        {
            *pos2 = '\0';
            sscanf(pos,"%s",hostname);
            *pos2 = '/';
            sscanf(pos2,"%s",path);
        }
        else
        {
            sscanf(pos,"%s",hostname);
        }
    }
    return;
}