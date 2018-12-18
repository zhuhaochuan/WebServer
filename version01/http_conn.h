/*
 * http_conn.h
 *
 *  Created on: 2018年12月17日
 *      Author: zhc
 */

#ifndef VERSION01_HTTP_CONN_H_
#define VERSION01_HTTP_CONN_H_

#include <unistd.h>
#include <signal.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;//文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;//读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;//写缓冲区的大小
    //HTTP请求的方法 这里只支持GET方法
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };
    //主机所处的状态　当前正在分析请求行，当前正在分析请求头，当前正在分析请求体
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    //HTTP处理的可能的结果
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    //行的读取状态　　读取到一个完整的行　出错行　行的数据尚不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn(){}
    ~http_conn(){}

public:
    //初始化新接受的连接
    void init( int sockfd, const sockaddr_in& addr );
    //关闭连接
    void close_conn( bool real_close = true );
    //处理客户端请求
    void process();
    //非阻塞的读
    bool read();
    //非阻塞的写
    bool write();

private:
    //初始化连接
    void init();
    //解析HTTP请求
    HTTP_CODE process_read();
    //填充HTTP应答
    bool process_write( HTTP_CODE ret );

    //下面这一组被process_read用于分析HTTP请求
    HTTP_CODE parse_request_line( char* text );
    HTTP_CODE parse_headers( char* text );
    HTTP_CODE parse_content( char* text );
    HTTP_CODE do_request();
    char* get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();

    //下面这组被用于填充ＨＴＴＰ应答
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

public:
    //这个epoll是指定的内核事件表，所有的ｓｏｃｋｅｔ事件都被注册到这个内核事件表上
    static int m_epollfd;
    //用户的数量
    static int m_user_count;

private:
    //读HTTP连接的socket　和对方的地址
    int m_sockfd;
    sockaddr_in m_address;
    //读缓冲区
    char m_read_buf[ READ_BUFFER_SIZE ];
    //可读的下一个位置
    int m_read_idx;
    //当前正在分析处理的字符在读缓冲区的位置
    int m_checked_idx;
    //当前正在解析的行的位置
    int m_start_line;
    //写缓冲区
    char m_write_buf[ WRITE_BUFFER_SIZE ];
    //写缓冲区待发送的字节数
    int m_write_idx;

    //主机当前所处的状态
    CHECK_STATE m_check_state;
    //请求的方法
    METHOD m_method;

    //客户请求的目标文件的完整路径
    char m_real_file[ FILENAME_LEN ];
    //客户端请求的文件名
    char* m_url;
    //HTTP协议版本号　　这里只支持１．１
    char* m_version;
    //主机名
    char* m_host;
    //HTTP　请求的消息体总长度
    int m_content_length;
    //HTTP 请求是否需要保持连接
    bool m_linger;

    //客户请求的目标文件被映射到内存当中的起始位置
    char* m_file_address;
    //目标文件的状态，判断文件是否存在，是否为目录，是否可读，文件大小等
    struct stat m_file_stat;
    //写内存块
    struct iovec m_iv[2];
    int m_iv_count;
};


#endif /* VERSION01_HTTP_CONN_H_ */
