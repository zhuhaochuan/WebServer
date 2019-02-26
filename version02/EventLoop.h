/*
 * EventLoop.h
 *
 *  Created on: 2018年12月20日
 *      Author: zhc
 */

#ifndef VERSION02_EVENTLOOP_H_
#define VERSION02_EVENTLOOP_H_

#include <pthread.h>

class Eventloop {
public:
	Eventloop();
	~Eventloop();

	void loop();
	void assertInLoopThread(){
		if(!isInLoopThread()) {
			abortNotInLoopThread();
		}
	}

	bool isInLoopThread() const {
		return threadId_ == CurrentThread::tid();
	}

private:
	void abortNotInLoopThread();
	bool looping_;
	const pid_t threadId_;
};



#endif /* VERSION02_EVENTLOOP_H_ */
