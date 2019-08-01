#ifndef UTILS_H
#define UTILS_H

#include <QReadWriteLock>
#include <QString>
#include <QHash>
#include <unordered_map>
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
	const T& operator*() { ensureLocked(); return data; }
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
// std::experimental::erase_if
template<class Key, class T, class Compare, class Alloc, class Pred>
void erase_if(std::unordered_map<Key,T,Compare,Alloc>& c, Pred pred)
{
	for (auto it = c.begin(), last = c.end(); it != last;) {
		if (pred(it)) {
			it = c.erase(it);
		} else {
			++it;
		}
	}
}

// roughly comparing float numbers. Most usable for GUI stuff
template<typename T>
bool almost_equal(const T& a, const T& b) {
	return std::abs(a - b) < 0.0001*std::abs(a);
}

#endif
