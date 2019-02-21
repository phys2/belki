#ifndef UTILS_H
#define UTILS_H

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

#endif
