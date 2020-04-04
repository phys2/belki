#ifndef UTILS_H
#define UTILS_H

#include <QReadWriteLock>
#include <QString>
#include <QHash>
#include <QMetaType>
#include <unordered_map>
#include <functional>

struct GuiMessage { // modeled after QMessageBox
	QString text;
	QString informativeText = {};
	enum { INFO, WARNING, CRITICAL } type = CRITICAL;
};

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
	explicit View(const T &d) : View(d, d.l) {}
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

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
// https://github.com/qt/qtbase/commit/4469e36d7203a55a4e158a50f0e9effc3f2fa3c2
#define QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH(Class, Arguments)      \
    QT_BEGIN_INCLUDE_NAMESPACE                                      \
    namespace std {                                                 \
        template <>                                                 \
        struct hash< QT_PREPEND_NAMESPACE(Class) > {                \
            using argument_type = QT_PREPEND_NAMESPACE(Class);      \
            using result_type = size_t;                             \
            size_t operator()(Arguments s) const                    \
                noexcept(noexcept(QT_PREPEND_NAMESPACE(qHash)(s)))  \
            {                                                       \
                /* this seeds qHash with the result of */           \
                /* std::hash applied to an int, to reap */          \
                /* any protection against predictable hash */       \
                /* values the std implementation may provide */     \
                return QT_PREPEND_NAMESPACE(qHash)(s,               \
                           QT_PREPEND_NAMESPACE(qHash)(             \
                                      std::hash<int>{}(0)));        \
            }                                                       \
        };                                                          \
    }                                                               \
    QT_END_INCLUDE_NAMESPACE                                        \
    /*end*/

#define QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH_BY_CREF(Class) \
    QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH(Class, const argument_type &)
#define QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH_BY_VALUE(Class) \
    QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH(Class, argument_type)

QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH_BY_CREF(QString)
QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH_BY_CREF(QStringRef)
QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH_BY_VALUE(QStringView)
QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH_BY_VALUE(QLatin1String)
QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH_BY_CREF(QByteArray)
QT_SPECIALIZE_STD_HASH_TO_CALL_QHASH_BY_CREF(QBitArray)
#endif

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
