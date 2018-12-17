/*
 * threadpool.h
 *线程池
 *  Created on: 2018年12月17日
 *      Author: zhc
 */

#ifndef VERSION01_THREADPOOL_H_
#define VERSION01_THREADPOOL_H_

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "locker.h"

template <typename T>
class threadpool {
public:
	threadpool(int thread_number = 8,int max_requests = 10000);
	~threadpool();
	bool append(T* request);
private:
	static void* worker(void* arg);
	void run();

	int m_thread_number;//线程池当中线程数量
	int m_max_requests;//请求队列当中最大的请求数目
	pthread_t* m_threads;//线程池数组
	std::list<T*> m_workqueue;//请求队列
	locker m_queuelocker;//保护请求队列的互斥锁
	sem m_queuestat;//是否有任务需要处理
	bool m_stop;//是否结束线程
};

template< typename T>
threadpool<T>::threadpool(int thread_number,int max_request):
	m_thread_number(thread_number),m_max_requests(max_request),m_stop(false),m_threads(NULL) {
	if((thread_number<=0) || (m_max_requests <= 0)){//错误的输入
		throw std::exception();
	}

	m_threads = new pthread_t[m_thread_number];//构建线程池数组 堆上创建
	if(!m_threads) {//异常处理 防止没有空间创建
		throw std::exception();
	}

	for(int i=0;i<thread_number;++i) {
		printf("create the %dth thread\n",i);
		//将线程池的指针当作参数传递进工作线程的工作函数
		if(pthread_create(m_threads+i,NULL,worker,this) !=0) {//如果创建失败 销毁已经创建的线程
			delete [] m_threads;
			throw std::exception();
		}
		if(pthread_detach(m_threads[i])) {//设置成为脱离线程
			delete []m_threads;
			throw std::exception();
		}
	}

}

template< typename T>
threadpool<T>::~threadpool() {
	delete [] m_threads;
	m_stop = true;
}

template< typename T>
bool threadpool<T>::append(T* request) {
	m_queuelocker.lock();
	if(m_workqueue.size() > m_max_requests) {
		m_queuelocker.unlock();
		return false;
	}
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post();//将信号量加一 如果大于0则唤醒阻塞在该信号量上的线程
	return true;
}

//线程的工作函数
template< typename T>
void* threadpool<T>::worker(void* arg) {
	threadpool* pool = (threadpool*) arg;
	pool->run();
	return pool;
}

template< typename T>
void threadpool<T> ::run() {
	while(!m_stop) {
		m_queuestat.wait();//将信号量的值减去1 如果信号量小于等于0则阻塞 直到大于0
		m_queuelocker.lock();
		if(m_workqueue.empty()) {
			m_queuelocker.unlock();
			continue;
		}
		T* request = m_workqueue.front();
		m_queuelocker.unlock();
		if(!request) {
			continue;
		}
		request->process();
	}
}


#endif /* VERSION01_THREADPOOL_H_ */
