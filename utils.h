#ifndef UTILS_H
#define UTILS_H

#include <QReadWriteLock>
#include <QString>
#include <QHash>
#include <functional>

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

struct RWLockable {
	mutable QReadWriteLock l{QReadWriteLock::RecursionMode::Recursive};
};

template<typename T>
struct View {
	View(const T &d, QReadWriteLock &l) : data(d), l(l) { l.lockForRead(); }
	View(const T &d) : View(d, d.l) {}
	View(const View&) = delete;
	View(View&& o) : data(o.data), l(o.l) {}
	~View() { unlock(); }
	const T& operator()() { ensureLocked(); return data; }
	const T* operator->() { ensureLocked(); return &data; }
	void ensureLocked() {
		if (!locked) throw std::runtime_error("Data access without proper lock.");
	}
	void unlock() { if (locked) l.unlock(); locked = false; }
protected:
	bool locked = true;
	const T &data;
	QReadWriteLock &l;
};

namespace std {
  template<> struct hash<QString> {
    std::size_t operator()(const QString& s) const {
      return qHash(s);
    }
  };
}

#endif
