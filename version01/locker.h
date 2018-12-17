/*
 * locker.h
 *linux 高性能服务器 没有使用RAII 机制封装的基本类
 *  Created on: 2018年12月17日
 *      Author: zhc
 */

#ifndef VERSION01_LOCKER_H_
#define VERSION01_LOCKER_H_

#include <exception>
#include <pthread.h>
#include <semaphore.h>

/*
 * 封装信号量的类
 */
class sem {
public:
	sem() {
		if(sem_init(&m_sem,0,0)!=0) {
			throw std::exception();
		}
	}
	~sem() {
		sem_destroy(&m_sem);
	}

	bool wait() {
		return sem_wait(&m_sem) == 0;
	}
	bool post() {
		return sem_post(&m_sem) == 0;
	}
private:
	sem_t m_sem;
};

/*
 * 封装互斥锁的类
 */

class locker {
public:
	locker() {
		if(pthread_mutex_init(&m_mutex,NULL)!=0) {
			throw std::exception();
		}
	}
	~locker() {
		pthread_mutex_destroy(&m_mutex);
	}
	bool lock() {
		return pthread_mutex_lock(&m_mutex)==0;
	}

	bool unlock() {
		return pthread_mutex_unlock(&m_mutex)==0;
	}
private:
	pthread_mutex_t m_mutex;
};
/*
 * 封装的条件变量类
 */
class cond {
public:
	cond() {
		if(pthread_mutex_init(&m_mutex, NULL) != 0) {
			throw std::exception();
		}
		if(pthread_cond_init(&m_cond,NULL)!=0) {
			pthread_mutex_destroy(&m_mutex);
			throw std::exception();
		}
	}
	~cond() {
		pthread_mutex_destroy(&m_mutex);
		pthread_cond_destroy(&m_cond);
	}
	bool wait() {
		int ret = 0;
		pthread_mutex_lock(&m_mutex);
		ret = pthread_cond_wait(&m_cond, &m_mutex);
		pthread_mutex_unlock(&m_mutex);
		return ret == 0;
	}

	bool signal() {
		return pthread_cond_signal(&m_cond)==0;
	}
private:
	pthread_mutex_t m_mutex;//保护条件变量的互斥锁
	pthread_cond_t m_cond;
};


















#endif /* VERSION01_LOCKER_H_ */
