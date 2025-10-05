#include "lib.h"

#include <iostream>

void foo() {
    std::cout << "foo" << std::endl;
}

void bar() {
    std::cout << "bar" << std::endl;
}

void MyClass::do_something() {
    std::cout << "MyClass do_something" << std::endl;
    do_something_not_exported();
}

void MyClass::do_something_not_exported() {
    std::cout << "MyClass do_something_not_exported" << std::endl;
}