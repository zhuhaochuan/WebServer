/*
 * noncopyable.h
 *
 *  Created on: 2018年12月20日
 *      Author: zhc
 */

#ifndef VERSION02_NONCOPYABLE_H_
#define VERSION02_NONCOPYABLE_H_
/*
 * 定义一个拷贝构造函数和赋值运算操作符都是私有成员函数这样
 * 所有继承自这个了类的所有的子类，所有的拷贝构造函数都是私有的。
 * 注意的一点就是子类如果重写自己的拷贝构造函数一定是因为这个类封装了指针类型的数据，需要深拷贝
 * 如果一个类不定义自己的拷贝拷贝构造函数，那么会有默认构造函数
 *默认的拷贝构造函数是先调用父类的拷贝构造函数　构造父类的部分　再构造自己的部分
 */
class noncopyable {
protected:
	noncopyable();
	~noncopyable();
private:
	noncopyable(const noncopyable&);
	const noncopyable operator=(const noncopyable&);
};



#endif /* VERSION02_NONCOPYABLE_H_ */
