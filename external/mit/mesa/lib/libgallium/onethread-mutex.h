/*
 * std::mutex for Mesa's ASTC tables (util/texcompress_astc_luts.h), where
 * MEECHO's libstdc++ has none: it is built single-threaded (gthr-single),
 * libmthread's threads being user-level.
 *
 * The tables are built lazily, once, under the lock, by code that calls
 * nothing that could switch libmthread's threads - they switch only inside
 * libmthread's own calls - so no other thread can run in the guarded
 * section, and a lock that does nothing is exact there.  Should libstdc++
 * get threads (a gthr for libmthread), this steps aside.
 */
#ifndef _MEECHO_ONETHREAD_MUTEX_H_
#define _MEECHO_ONETHREAD_MUTEX_H_

#include <bits/c++config.h>

#if !defined(_GLIBCXX_HAS_GTHREADS)
namespace std {
struct mutex {
	constexpr mutex() noexcept {}
	mutex(const mutex &) = delete;
	mutex &operator=(const mutex &) = delete;
	void lock() {}
	bool try_lock() { return true; }
	void unlock() {}
};
}
#endif

#endif
