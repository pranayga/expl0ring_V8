/**
 * This file gives a simple example of a usecase where rvalue references
 * actually shine.
 */
#include <iostream>

class Vector {
public:
     Vector(int s) :elem{new double[s]}, sz{s} { }   // construct a Vector
     double& operator[](int i) { return elem[i]; }   // element access: subscripting
     int size() { return sz; }
private:
     double* elem; // pointer to the elements
     int sz;       // the number of elements
};

int main(){
    std::cout<<"Test String";
}

