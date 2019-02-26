/*
 * main.cpp
 *
 *  Created on: 2018年12月17日
 *      Author: zhc
 */


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd( int epollfd, int fd, bool one_shot );
extern int removefd( int epollfd, int fd );

void addsig( int sig, void( handler )(int), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if( restart )
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void show_error( int connfd, const char* info )
{
    printf( "%s", info );
    send( connfd, info, strlen( info ), 0 );
    close( connfd );
}


int main( int argc, char* argv[] )
{
	//需要2个参数，ip,port 在本机上那个端口起服务
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    addsig( SIGPIPE, SIG_IGN );
    //创建线程池 业务逻辑由http_conn负责
    threadpool< http_conn >* pool = NULL;
    try
    {
        pool = new threadpool< http_conn >;
    }
    catch( ... )
    {
        return 1;
    }

    //用户数组　一个非常大的桶可以放65535个连接　使用文件描述符进行映射。
    http_conn* users = new http_conn[ MAX_FD ];
    assert( users );
    int user_count = 0;

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );//创建一个文件描述符
    assert( listenfd >= 0 );
    struct linger tmp = { 1, 0 };
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );

    int ret = 0;
    //设置监听地址
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    //命名socket将地址分配给ie文件描述符
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret >= 0 );

    //后台监听该文件描述符
    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    //使用epoll作为IO复用事件循环 主线程监听本地端口
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    addfd( epollfd, listenfd, false );
    //设定所有客户连接的绑定的内核事件表为主线程创建的事件表
    http_conn::m_epollfd = epollfd;

    while( true )
    {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        //多路复用逻辑 遍历events当中所有的文件描述符
        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            //如果是本地端口有数据说明是 有用户需要建立连接
            if( sockfd == listenfd )
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                if( http_conn::m_user_count >= MAX_FD )
                {
                    show_error( connfd, "Internal server busy，现在外部服务器很忙！" );
                    continue;
                }
                //客户连接的初始化当中会将起注册到内核事件表当中
                users[connfd].init( connfd, client_address );
            }
            //如果是用户出问题，挂起，对方关闭连接 就关闭该连接
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )
            {
                users[sockfd].close_conn();//关闭连接在内核事件表当中删除该文件描述符
            }
            //如果是用户文件描述符上有事件发生，将对应的读写事件加入线程池的阻塞队列当中交给线程池处理
            else if( events[i].events & EPOLLIN )
            {
                if( users[sockfd].read() )//主线程去将数据从内核态缓冲区读入用户缓冲区
                {
                	//线程池内部自己维持一个信号量　进行自动的生产者消费者运作
                	//线程池当中的线程去处理已经读好的数据
                    pool->append( users + sockfd );
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }
            else if( events[i].events & EPOLLOUT )
            {
            	//将用户缓冲区的数据写入内核缓冲区　如果写入出现问题或者是短连接就关闭连接。
                if( !users[sockfd].write() )
                {
                    users[sockfd].close_conn();
                }
            }
            else
            {}
        }
    }

    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;
}


