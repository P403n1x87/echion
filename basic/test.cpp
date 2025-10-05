#include <iostream>
#include "lib.h"

void bar() {
    std::cout << "fake bar" << std::endl;
}

void MyClass::do_something_not_exported() {
    std::cout << "fake do_something_not_exported" << std::endl;
}

void MyClass::something_else() {
    std::cout << "MyClass fake something_else" << std::endl;
}

int main() {
    foo();
    bar();
    MyClass my_class;
    my_class.do_something();
    my_class.do_something_not_exported();
    my_class.something_else();
    return 0;
}