// C++ on MEECHO (stage 11.6: Mesa's GLSL compiler and state tracker are
// C++): what a program built by g++ and libstdc++ from
// ~/xtools-aarch64-cxx needs of the system, statically linked as the rest
// of the userland is.  Each check prints a line; the last line counts them.
//
//	cxxprobe	containers and strings, iostreams, a static
//			constructor, a function-local static, thread_local,
//			exceptions thrown and caught (the unwinder has to find
//			the program's frames: no .eh_frame_hdr in a static
//			link, so crtbegin's registration), std::mutex (when
//			libstdc++ has threads), std::sort, std::map, virtual
//			calls and dynamic_cast
//
// Not here: new beyond what the process can have.  An allocation of 1 TiB
// takes the whole system down (VM runs out of its own memory, VFS dies)
// - a defect of VM's, not of C++, followed up on its own.

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static int checks, failures;

static void
check(bool ok, const char *what)
{
	checks++;
	if (!ok)
		failures++;
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	std::fflush(stdout);
}

struct Early {
	int v;
	Early() : v(42) {}
};
static Early early;

static int
counted()
{
	static int n = [] { return 7; }();
	return n;
}

static thread_local std::string tls_str = "thread-local";

struct Shape {
	virtual ~Shape() {}
	virtual int sides() const = 0;
};
struct Square : Shape {
	int sides() const override { return 4; }
};
struct Tri : Shape {
	int sides() const override { return 3; }
};

static int
thrower(int depth)
{
	if (depth == 0)
		throw std::runtime_error("deep");
	return thrower(depth - 1) + 1;
}

int
main()
{
	std::vector<std::string> v{"gamma", "alpha", "beta"};
	std::sort(v.begin(), v.end());
	check(v[0] == "alpha" && v[2] == "gamma", "vector<string> sorted");

	std::map<std::string, int> m{{"b", 2}, {"a", 1}};
	check(m.begin()->first == "a" && m["b"] == 2, "map");

	std::ostringstream os;
	os << "x=" << 12 << ' ' << 1.5;
	check(os.str() == "x=12 1.5", "ostringstream");
	std::cout << "iostream: cout works" << std::endl;
	check(std::cout.good(), "cout");

	check(early.v == 42, "a static constructor ran");
	check(counted() == 7 && counted() == 7, "a function-local static");
	check(tls_str == "thread-local", "a thread_local std::string");

	bool caught = false;
	try {
		thrower(20);
	} catch (const std::runtime_error &e) {
		caught = std::string(e.what()) == "deep";
	}
	check(caught, "an exception through 20 frames");

	caught = false;
	try {
		std::vector<int> x;
		(void)x.at(3);
	} catch (const std::out_of_range &) {
		caught = true;
	}
	check(caught, "std::out_of_range from the library");

#if defined(_GLIBCXX_HAS_GTHREADS)
	std::mutex mu;
	{
		std::lock_guard<std::mutex> g(mu);
	}
	check(mu.try_lock(), "std::mutex");
	mu.unlock();
#else
	std::printf("info no std::mutex: libstdc++ is single-threaded "
	    "(gthr-single)\n");
#endif

	std::unique_ptr<Shape> s1(new Square), s2(new Tri);
	check(s1->sides() + s2->sides() == 7, "virtual calls");
	check(dynamic_cast<Square *>(s1.get()) != nullptr &&
	    dynamic_cast<Square *>(s2.get()) == nullptr, "dynamic_cast");

	std::printf("CXXPROBE %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
