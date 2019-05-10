#ifndef UTILS_H
#define UTILS_H

#include <QReadWriteLock>

class NonCopyable
{
public: 
	NonCopyable(const NonCopyable&) = delete;
	NonCopyable& operator=(const NonCopyable &) = delete;

protected:
	NonCopyable() = default;
	~NonCopyable() = default; /// Protected non-virtual destructor
};

struct OnlyMovable
{
	OnlyMovable(const OnlyMovable&) = delete;
	OnlyMovable& operator=(const OnlyMovable&) = delete;
	OnlyMovable(OnlyMovable&&) = default;
	OnlyMovable& operator=(OnlyMovable&&) = default;

protected:
	OnlyMovable() = default;
};

template<typename T>
struct View {
	View(const T &d, QReadWriteLock &l) : data(d), l(l) { l.lockForRead(); }
	View(const View&) = delete;
	View(View&& o) : data(o.data), l(o.l) {}
	~View() { l.unlock(); }
	const T& operator()() { return data; }
	const T* operator->() { return &data; }
protected:
	const T &data;
	QReadWriteLock &l;
};

#endif
