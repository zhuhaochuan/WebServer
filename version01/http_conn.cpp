/*
 * http_conn.cpp
 *
 *  Created on: 2018年12月17日
 *      Author: zhc
 */

#include "http_conn.h"
#include <iostream>

using namespace std;
//HTTP相应的一些状态信息
const char* ok_200_title = "OK";

const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";

const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";

const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";

const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
const char* doc_root = "/var/www/html";//所有的文件需要放在系统的这个路径下 在url当中不需要给出这个路径前缀

//将描述符设置成为非阻塞的
int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    //fcntl　提供对文件描述符的各种操作　　通常用来设置文件描述符为非阻塞
    return old_option;
}

//将文件描述符fd上的事件注册到epoll内核事件表当中
void addfd( int epollfd, int fd, bool one_shot )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;//采用的是ET边沿触发方式
    if( one_shot )//一个socket上的事件只能被触发一次
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}
//删除内核事件表当中的fd
void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
}
//修改fd上的事件
void modfd( int epollfd, int fd, int ev )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}
//初始化这两个静态成员
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn( bool real_close )
{
    if( real_close && ( m_sockfd != -1 ) )
    {
        //modfd( m_epollfd, m_sockfd, EPOLLIN );
        removefd( m_epollfd, m_sockfd );
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init( int sockfd, const sockaddr_in& addr )
{
    m_sockfd = sockfd;
    m_address = addr;
    int error = 0;
    socklen_t len = sizeof( error );
    getsockopt( m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len );
    int reuse = 1;
    setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    addfd( m_epollfd, sockfd, true );
    m_user_count++;

    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    //几个缓冲区数组的初始化　全部初始化为空
    memset( m_read_buf, '\0', READ_BUFFER_SIZE );
    memset( m_write_buf, '\0', WRITE_BUFFER_SIZE );
    memset( m_real_file, '\0', FILENAME_LEN );
}

//从状态机　用于解析出一行的内容
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    //从当前处理字符在读缓冲区的位置开始，直到客户数据尾部的下一个字节　m_checked_idx之前的字符全部被解析完毕
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx )
    {
    	//一个字符一个字符的解析
        temp = m_read_buf[ m_checked_idx ];
        //如果当前是回车　说明可能是一个完整的行
        if ( temp == '\r' )
        {
        	//如果回车已经是目前buffer当中的最后一个已经读入的字符　那么这次没有分析到一个完整的行　需要进一步读取用户数据
            if ( ( m_checked_idx + 1 ) == m_read_idx )
            {
                return LINE_OPEN;
            }
            //如果下一个字符是换行　就说明成功读到一个完整的行
            else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' )
            {
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            //否则用户的输入有语法错误
            return LINE_BAD;
        }
        //如果当前得到的是一个换行　也可能是读取到了一个完整的行
        else if( temp == '\n' )
        {
        	//如果不是buffer的第一个元素并且　前一个字符是回车那么就是一个完整的行
            if( ( m_checked_idx > 1 ) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) )
            {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    //没有读到回车或者换行　那么就是还需要读取用户数据　还没读完整。
    return LINE_OPEN;
}

//循环读取客户数据　直到无数据可读或者对方关闭连接
//将数据从内核态读入用户态　不做任何计算和处理　这里是非阻塞的读
bool http_conn::read()
{
	//没有可读内容
    if( m_read_idx >= READ_BUFFER_SIZE )
    {
        return false;
    }

    int bytes_read = 0;
    while( true )
    {
    	//往读缓冲区读用户数据　文件描述符　读缓冲区的位置和大小　flags参数
        bytes_read = recv( m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );
        if ( bytes_read == -1 )//出错返回－１
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )//表示当前没有数据可读　稍后再尝试　也就是当前的读事件带来的数据已经被读完了
            {
                break;//由于是非阻塞的读　所以此处跳出　循环
            }
            return false;
        }
        else if ( bytes_read == 0 )//返回值为０　说明对方已经关闭连接
        {
            return false;
        }
        //否则的话　bytes_read　大小为实际读取到的数据长度
        m_read_idx += bytes_read;
    }
    return true;
}

//分析请求行
http_conn::HTTP_CODE http_conn::parse_request_line( char* text )
{
	cout << "text " << text << endl;//GET /welcome.html HTTP/1.1
    m_url = strpbrk( text, " \t" );//将text按照给定的字符进行拆分　返回第一个出现空格之后的位置指针
    cout << m_url << endl;
    //找test当中是否有空格或者\t　如果没有那么这个请求必然有问题
    if ( ! m_url )
    {
        return BAD_REQUEST;
    }

    *m_url++ = '\0';//将空格填充　隔开两个字符串

    char* method = text;
    cout << "text " << text << endl;//GET
    if ( strcasecmp( method, "GET" ) == 0 )//比较两个字符串的前n个字符串 如果完全相等返回０
    {
        m_method = GET;
    }
    else
    {
    	//只支持GET 其他的都是错误的请求
        return BAD_REQUEST;
    }

    m_url += strspn( m_url, " \t" );
    cout << m_url << endl;
    m_version = strpbrk( m_url, " \t" );
    if ( ! m_version )
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn( m_version, " \t" );
    if ( strcasecmp( m_version, "HTTP/1.1" ) != 0 )
    {
        return BAD_REQUEST;
    }

    if ( strncasecmp( m_url, "http://", 7 ) == 0 )
    {
        m_url += 7;
        m_url = strchr( m_url, '/' );
    }

    if ( ! m_url || m_url[ 0 ] != '/' )
    {
        return BAD_REQUEST;
    }
    //请求行处理完毕　转到头部字段分析
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}
//分析头部字段
http_conn::HTTP_CODE http_conn::parse_headers( char* text )
{
    if( text[ 0 ] == '\0' )
    {
        if ( m_method == HEAD )
        {
            return GET_REQUEST;
        }

        if ( m_content_length != 0 )
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        return GET_REQUEST;
    }
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 )
    {
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 )
        {
            m_linger = true;
        }
    }
    else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 )
    {
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol( text );
    }
    else if ( strncasecmp( text, "Host:", 5 ) == 0 )
    {
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    }
    else
    {
        printf( "oop! unknow header %s\n", text );
    }

    return NO_REQUEST;

}

http_conn::HTTP_CODE http_conn::parse_content( char* text )
{
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

//分析HTTP 请求的入口函数
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    //主状态机
    while ( ( ( m_check_state == CHECK_STATE_CONTENT ) && ( line_status == LINE_OK  ) )
                || ( ( line_status = parse_line() ) == LINE_OK ) )
    {
        text = get_line();
        m_start_line = m_checked_idx;
        printf( "got 1 http line: %s\n", text );

        //根据当前状态进行分析
        switch ( m_check_state )
        {
        	//分析请求行
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                break;
            }
            //分析请求头
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                else if ( ret == GET_REQUEST )
                {
                    return do_request();
                }
                break;
            }
            //分析请求体
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content( text );
                if ( ret == GET_REQUEST )
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default://如果其他的情况　那就是服务器内部错误
            {
                return INTERNAL_ERROR;
            }
        }
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    printf("文件路径为%s \n",m_real_file);
    if ( stat( m_real_file, &m_file_stat ) < 0 )
    {
        return NO_RESOURCE;
    }

    if ( ! ( m_file_stat.st_mode & S_IROTH ) )
    {
        return FORBIDDEN_REQUEST;
    }

    if ( S_ISDIR( m_file_stat.st_mode ) )
    {
        return BAD_REQUEST;
    }

    int fd = open( m_real_file, O_RDONLY );
    //将打卡的文件映射到这块内存空间当中
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

//写HTTP响应　将用户缓冲区的数据写入内核缓冲区　非阻塞的写
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if ( bytes_to_send == 0 )
    {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        init();
        return true;
    }

    while( 1 )
    {
    	//writev是集中写的函数　将几块不同的内存单元的内容　集中的写入目标的文件描述符
    	//这里是２块内存单元　一块是放的HTTP响应　一块是放的　返回给客户的文件的内容
        temp = writev( m_sockfd, m_iv, m_iv_count );
        if ( temp <= -1 )
        {
            if( errno == EAGAIN )//没写完　还需要再次写
            {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send )
        {
            unmap();
            if( m_linger )//如果是长连接
            {
                init();//读写缓冲区清空　等待下一次的数据到来
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            }
            else
            {
            	//短连接　外部会将这个用户连接删除　　但是还会监听该文件描述符的读事件？
                modfd( m_epollfd, m_sockfd, EPOLLIN );//这个貌似是没有必要的　因为外界会删除在内核事件表当中的该文件描述符
                return false;
            }
        }
    }
}

bool http_conn::add_response( const char* format, ... )
{
    if( m_write_idx >= WRITE_BUFFER_SIZE )
    {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) )
    {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status, const char* title )
{
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers( int content_len )
{
    add_content_length( content_len );
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length( int content_len )
{
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

//根据服务器处理请求的结果　决定返回给客户端的内容
bool http_conn::process_write( HTTP_CODE ret )
{
    switch ( ret )
    {
        case INTERNAL_ERROR:
        {
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) )
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) )
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) )
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line( 403, error_403_title );
            add_headers( strlen( error_403_form ) );
            if ( ! add_content( error_403_form ) )
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line( 200, ok_200_title );
            if ( m_file_stat.st_size != 0 )
            {
                add_headers( m_file_stat.st_size );
                m_iv[ 0 ].iov_base = m_write_buf;
                m_iv[ 0 ].iov_len = m_write_idx;
                m_iv[ 1 ].iov_base = m_file_address;
                m_iv[ 1 ].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else
            {
                const char* ok_string = "<html><body></body></html>";
                add_headers( strlen( ok_string ) );
                if ( ! add_content( ok_string ) )
                {
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

//由线程池当中的工作线程调用　处理HTTP请求的入口函数
void http_conn::process()
{
	//分析HTTP请求得到　响应的返回码
    HTTP_CODE read_ret = process_read();
    //如果是用户请求不完整　就修改该客户描述符的事件为等待读事件　继续读入。
    if ( read_ret == NO_REQUEST )
    {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }
    //得到了响应的返回码之后　根据响应码　给用户写响应
    bool write_ret = process_write( read_ret );
    if ( ! write_ret )
    {
        close_conn();
    }
    //在这里已经往用户缓冲区写好了　就修改事件　让主线程去写入内核缓冲区
    modfd( m_epollfd, m_sockfd, EPOLLOUT );
}




