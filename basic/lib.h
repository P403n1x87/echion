#pragma once

#include <iostream>

void foo();
void bar();

class MyClass {
    public:
        void do_something();
        void do_something_not_exported();

        void something_else();
};

void MyClass::something_else() {
    std::cout << "MyClass something_else" << std::endl;
}