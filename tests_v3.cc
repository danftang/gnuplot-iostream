#include <fstream>
#include <vector>
#include <stdexcept>

#include <boost/utility.hpp>
#include <boost/array.hpp>
#include <boost/mpl/and.hpp>
#include <boost/mpl/logical.hpp>

////////////////////////////////////////////////////////////
/// Debugging functions, to be removed
////////////////////////////////////////////////////////////

// for debugging
#include <typeinfo>
#include <cxxabi.h>

template <typename T>
std::string get_typename() {
	int status;
	char *name;
	name = abi::__cxa_demangle(typeid(T).name(), 0, 0, &status);
	assert(!status);
	return std::string(name);
}

////////////////////////////////////////////////////////////
/// Template magic
////////////////////////////////////////////////////////////

template <typename T>
class is_like_stl_container {
    typedef char one;
    typedef long two;

    template <typename C> static one test(typename C::value_type *, typename C::const_iterator *);
    template <typename C> static two test(...);

public:
    enum { value = sizeof(test<T>(NULL, NULL)) == sizeof(char) };
	typedef boost::mpl::bool_<value> type;
};

template<typename T> struct has_attrib_n_rows {
    struct Fallback { int n_rows; };
    struct Derived : T, Fallback { };

    template<typename C, C> struct ChT;

    template<typename C> static char (&f(ChT<int Fallback::*, &C::n_rows>*))[1];
    template<typename C> static char (&f(...))[2];

    static bool const value = sizeof(f<Derived>(0)) == 2;
	typedef boost::mpl::bool_<value> type;
};

template<typename T> struct has_attrib_n_cols {
    struct Fallback { int n_cols; };
    struct Derived : T, Fallback { };

    template<typename C, C> struct ChT;

    template<typename C> static char (&f(ChT<int Fallback::*, &C::n_cols>*))[1];
    template<typename C> static char (&f(...))[2];

    static bool const value = sizeof(f<Derived>(0)) == 2;
	typedef boost::mpl::bool_<value> type;
};

template <typename T>
struct is_armadillo_mat {
	static const bool value = has_attrib_n_rows<T>::value && has_attrib_n_cols<T>::value;
	typedef boost::mpl::bool_<value> type;
};

// Error messages involving this stem from treating something that was not a container as if it
// was.  This is only here to allow compiliation without errors in normal cases.
class WasNotContainer { };

template <typename T, typename Enable=void>
class ArrayTraits {
public:
	typedef WasNotContainer value_type;
	typedef WasNotContainer range_type;
	static const bool is_container = false;
	static const size_t depth = 0;
	static const size_t ncols = 1;

	static range_type get_range(const T &) {
		throw std::invalid_argument("argument was not a container");
	}
};

template <typename V>
class ArrayTraitsDefaults {
public:
	typedef V value_type;
	static const bool is_container = true;
	static const size_t depth = ArrayTraits<V>::depth + 1;
	static const size_t ncols = 1;
};

template <typename TI, typename TV>
class IteratorRange {
public:
	IteratorRange() { }
	IteratorRange(const TI &_it, const TI &_end) : it(_it), end(_end) { }

	typedef TV value_type;
	typedef typename ArrayTraits<TV>::range_type subiter_type;
	static const bool is_container = ArrayTraits<TV>::is_container;

	bool is_end() { return it == end; }

	void inc() { ++it; }

	value_type deref() const {
		return *it;
	}

	subiter_type deref_subiter() const {
		return ArrayTraits<TV>::get_range(*it);
	}

private:
	TI it, end;
};

template <typename T>
class ArrayTraits<T, typename boost::enable_if<
	boost::mpl::and_<
		is_like_stl_container<T>,
		boost::mpl::not_<is_armadillo_mat<T> >
	>
>::type> : public ArrayTraitsDefaults<typename T::value_type> {
public:
	typedef IteratorRange<typename T::const_iterator, typename T::value_type> range_type;

	static range_type get_range(const T &arg) {
		return range_type(arg.begin(), arg.end());
	}
};

template <typename T, size_t N>
class ArrayTraits<T[N]> : public ArrayTraitsDefaults<T> {
public:
	typedef IteratorRange<const T*, T> range_type;

	static range_type get_range(const T (&arg)[N]) {
		return range_type(arg, arg+N);
	}
};

template <typename T, typename U>
class ArrayTraits<std::pair<T, U> > {
	template <typename RT, typename RU>
	class PairOfRange {
	public:
		PairOfRange() { }
		PairOfRange(const RT &_l, const RU &_r) : l(_l), r(_r) { }

		static const bool is_container = RT::is_container && RU::is_container;

		typedef std::pair<typename RT::value_type, typename RU::value_type> value_type;
		typedef PairOfRange<typename RT::subiter_type, typename RU::subiter_type> subiter_type;

		bool is_end() {
			bool el = l.is_end();
			bool er = r.is_end();
			if(el != er) {
				throw std::length_error("columns were different lengths");
			}
			return el;
		}

		void inc() {
			l.inc();
			r.inc();
		}

		value_type deref() const {
			return std::make_pair(l.deref(), r.deref());
		}

		subiter_type deref_subiter() const {
			return subiter_type(l.deref_subiter(), r.deref_subiter());
		}

	private:
		RT l;
		RU r;
	};

public:
	typedef PairOfRange<typename ArrayTraits<T>::range_type, typename ArrayTraits<U>::range_type> range_type;
	typedef std::pair<typename ArrayTraits<T>::value_type, typename ArrayTraits<U>::value_type> value_type;
	static const bool is_container = ArrayTraits<T>::is_container && ArrayTraits<U>::is_container;
	// It is allowed for l_depth != r_depth, for example one column could be 'double' and the
	// other column could be 'vector<double>'.
	static const size_t l_depth = ArrayTraits<T>::depth;
	static const size_t r_depth = ArrayTraits<U>::depth;
	static const size_t depth = (l_depth < r_depth) ? l_depth : r_depth;
	static const size_t ncols = ArrayTraits<T>::ncols + ArrayTraits<U>::ncols;

	static range_type get_range(const std::pair<T, U> &arg) {
		return range_type(
			ArrayTraits<T>::get_range(arg.first),
			ArrayTraits<U>::get_range(arg.second)
		);
	}
};

template <typename RT>
class VecOfRange {
public:
	VecOfRange() { }
	VecOfRange(const std::vector<RT> &_rvec) : rvec(_rvec) { }

	static const bool is_container = RT::is_container;

	typedef std::vector<typename RT::value_type> value_type;
	typedef VecOfRange<typename RT::subiter_type> subiter_type;

	bool is_end() {
		if(rvec.empty()) return true;
		bool ret = rvec[0].is_end();
		for(size_t i=1; i<rvec.size(); i++) {
			if(ret != rvec[i].is_end()) {
				throw std::length_error("columns were different lengths");
			}
		}
		return ret;
	}

	void inc() {
		for(size_t i=0; i<rvec.size(); i++) {
			rvec[i].inc();
		}
	}

	value_type deref() const {
		value_type ret(rvec.size());
		for(size_t i=0; i<rvec.size(); i++) {
			ret[i] = rvec[i].deref();
		}
		return ret;
	}

	subiter_type deref_subiter() const {
		std::vector<typename RT::subiter_type> subvec(rvec.size());
		for(size_t i=0; i<rvec.size(); i++) {
			subvec[i] = rvec[i].deref_subiter();
		}
		return subiter_type(subvec);
	}

private:
	std::vector<RT> rvec;
};

// FIXME - arg could be any iterable type (e.g. 3d blitz array)
template <typename T>
VecOfRange<typename ArrayTraits<T>::range_type>
get_columns_range(const std::vector<T> &arg) {
	std::vector<typename ArrayTraits<T>::range_type> rvec(arg.size());
	for(size_t i=0; i<arg.size(); i++) {
		rvec[i] = ArrayTraits<T>::get_range(arg[i]);
	}
	return VecOfRange<typename ArrayTraits<T>::range_type>(rvec);
}

////////////////////////////////////////////////////////////
/// Armadillo support
////////////////////////////////////////////////////////////

// FIXME - detect if armadillo already included, and don't include otherwise.
// Or better yet, use template magic to avoid needing armadillo include.
#include <armadillo>

#if 1
template <typename T>
class ArrayTraits<arma::Mat<T> > : public ArrayTraitsDefaults<T> {
	class ArmaMatRange {
	public:
		ArmaMatRange() : p(NULL), it(0) { }
		ArmaMatRange(const arma::Mat<T> *_p) : p(_p), it(0) { }

		typedef T value_type;
		typedef IteratorRange<typename arma::Mat<T>::const_row_iterator, T> subiter_type;
		static const bool is_container = true;

		bool is_end() { return it == p->n_rows; }

		void inc() { ++it; }

		value_type deref() const {
			throw std::logic_error("can't call deref on an armadillo slice");
		}

		subiter_type deref_subiter() const {
			return subiter_type(p->begin_row(it), p->end_row(it));
		}

	private:
		const arma::Mat<T> *p;
		size_t it;
	};
public:
	typedef ArmaMatRange range_type;

	static range_type get_range(const arma::Mat<T> &arg) {
		return range_type(&arg);
	}
};

template <typename T>
class ArrayTraits<arma::Col<T> > : public ArrayTraitsDefaults<T> {
public:
	typedef IteratorRange<typename arma::Col<T>::const_iterator, T> range_type;

	static range_type get_range(const arma::Col<T> &arg) {
		return range_type(arg.begin(), arg.end());
	}
};
#endif

////////////////////////////////////////////////////////////
/// Begin plotting functions
////////////////////////////////////////////////////////////

template <typename T>
void print_entry(const T &arg) {
	std::cout << "[" << get_typename<T>() << ":" << arg << "]";
}

template <typename T>
void print_entry(const std::vector<T> &arg) {
	std::cout << "{";
	for(size_t i=0; i<arg.size(); i++) {
		if(i) std::cout << " ";
		print_entry(arg[i]);
	}
	std::cout << "}";
}

template <typename T, typename U>
void print_entry(const std::pair<T, U> &arg) {
	print_entry(arg.first);
	std::cout << " ";
	print_entry(arg.second);
}

template <typename T>
typename boost::disable_if_c<T::is_container>::type
print_block(T &arg) {
	while(!arg.is_end()) {
		print_entry(arg.deref());
		std::cout << std::endl;
		arg.inc();
	}
}

template <typename T>
typename boost::enable_if_c<T::is_container>::type
print_block(T &arg) {
	bool first = true;
	while(!arg.is_end()) {
		if(first) {
			first = false;
		} else {
			std::cout << std::endl;
		}
		std::cout << "<block>" << std::endl;
		typename T::subiter_type sub = arg.deref_subiter();
		print_block(sub);
		arg.inc();
	}
}

template <typename T>
void plot(std::string header, const T &arg) {
	std::cout << "--- " << header << " -------------------------------------" << std::endl;
	std::cout << "ncols=" << ArrayTraits<T>::ncols << std::endl;
	std::cout << "depth=" << ArrayTraits<T>::depth << std::endl;
	//std::cout << "range_type=" << get_typename<typename ArrayTraits<T>::range_type>() << std::endl;
	typename ArrayTraits<T>::range_type range = ArrayTraits<T>::get_range(arg);
	print_block(range);
	std::cout << "e" << std::endl;
}

template <typename T>
void plot_cols(std::string header, const std::vector<T> &arg) {
	std::cout << "--- " << header << " -------------------------------------" << std::endl;
	//std::cout << "ncols=" << ArrayTraits<T>::ncols << std::endl;
	//std::cout << "depth=" << ArrayTraits<T>::depth << std::endl;
	//std::cout << "range_type=" << get_typename<typename ArrayTraits<T>::range_type>() << std::endl;
	VecOfRange<typename ArrayTraits<T>::range_type> range = get_columns_range(arg);
	print_block(range);
	std::cout << "e" << std::endl;
}

int main() {
	const int NX=3, NY=4;
	std::vector<double> vd;
	std::vector<int> vi;
	std::vector<std::vector<double> > vvd(NX);
	std::vector<std::vector<int> > vvi(NX);
	std::vector<std::vector<std::vector<int> > > vvvi(NX);
	int ai[NX];
	boost::array<int, NX> bi;
	arma::vec armacol(NX);
	arma::mat armamat(NX, NY);

	for(int x=0; x<NX; x++) {
		vd.push_back(x+7.5);
		vi.push_back(x+7);
		ai[x] = x+7;
		bi[x] = x+70;
		armacol(x) = x+0.123;
		for(int y=0; y<NY; y++) {
			vvd[x].push_back(100+x*10+y);
			vvi[x].push_back(200+x*10+y);
			std::vector<int> tup;
			tup.push_back(300+x*10+y);
			tup.push_back(400+x*10+y);
			vvvi[x].push_back(tup);
			armamat(x, y) = x*10+y+0.123;
		}
	}

	plot("vd,vi,bi", std::make_pair(vd, std::make_pair(vi, bi)));
	plot("vvd,vvi", std::make_pair(vvd, vvi));
	plot("ai", ai);
	// FIXME - doesn't work because array gets cast to pointer
	//plot(std::make_pair(ai, bi));
	plot("vvd,vvi,vvvi", std::make_pair(vvd, std::make_pair(vvi, vvvi)));
	plot_cols("vvvi cols", vvvi);
	plot("armacol", armacol);
	plot("armamat", armamat);
}