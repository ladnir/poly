


#include <array>
#include <iostream>
#include <string>
#include "unique_ptr.h"
#include "unique_ptr_v2.h"


struct base
{
    virtual ~base() = default;
    virtual std::string to_string() = 0;
};

struct large : public base
{
    std::array<char, 1000> val = {};

    explicit large(std::string s) {
        std::copy(s.begin(), s.begin() + std::min<size_t>(s.size(), val.size() - 1), val.begin());
    }

    std::string to_string() override { return  "large: " + std::string(val.data()); }
};


struct small : public base
{
    size_t val;
    explicit small(size_t i) : val(i) {}

    std::string to_string() override { return "small: " + std::to_string(val); }
};


struct special_small : public small
{
    char c_val;
    special_small(int i, char c)
        : small(i)
        , c_val(c)
    {}

    std::string to_string() override { return "special small: " + std::to_string(val); }
};

template<typename Ptr>
void print(Ptr& ptr)
{
    std::cout << ptr->to_string() << " (local = " << ptr.is_inlined() << ")" << std::endl;
}

int main()
{
    using namespace poly_v2;
    unique_ptr<base> ptr;

    //unique_ptr<base> pp(in_place<large>{}, "hello");

    ptr.emplace<small>(2);
    print(ptr);


    small s(1);
    ptr = std::move(s);
    print(ptr);

    ptr.reset(new small(2));
    print(ptr);

    ptr.emplace<small>(3);
    print(ptr);

    large l("#1");
    ptr = std::move(l);
    print(ptr);

    ptr.reset(new large("#2"));
    print(ptr);

    ptr.emplace<large>("#3");
    print(ptr);

    unique_ptr<base, 10000> largePtr(std::move(ptr));
    print(largePtr);

    largePtr.emplace<small>(4);

    unique_ptr<base, 8> tinyPtr;


    tinyPtr = std::move(largePtr);
    print(tinyPtr);

    small* ss = new special_small(10, 'c');


    largePtr.reset(ss);

    ss = largePtr.release<small>();

    ss = new small(10);
    largePtr.reset(ss);

    return 0;
}

