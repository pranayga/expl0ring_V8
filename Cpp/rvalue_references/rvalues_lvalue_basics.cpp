// Reference : https://www.tutorialspoint.com/cplusplus/cpp_copy_constructor.htm#:~:text=The%20copy%20constructor%20is%20a,an%20argument%20to%20a%20function.
#include <iostream>

using namespace std;

class Line {

   public:
      int getLength( void );
      Line( int len );                  // simple constructor
      Line( const Line &obj);           // copy constructor
      ~Line();                          // destructor
      Line operator+(const Line &obj_b);// + operator

   private:
      int* ref_len;
};

// Member functions definitions including constructor
Line::Line(int len) {
   cout << "Normal constructor allocating ptr" << endl;
   
   // allocate memory for the pointer;
   ref_len = new int;
   cout << "Allocate space for " << len << endl;
   
   *ref_len = len;
}

Line::Line(const Line &obj) {
   cout << "Copy constructor allocating ptr." << endl;
   ref_len = new int;
   
   cout << "Allocate space for Copying " << *(obj.ref_len) << endl;
   *ref_len = *(obj.ref_len); // copy the value
}

Line Line::operator+(const Line &obj_b){
    cout << "Inside + operator. this:" << *(this->ref_len) << " obj_b:" <<*(obj_b.ref_len) << endl;
    Line temp_line = Line(*(this->ref_len) + *(obj_b.ref_len));
    return (temp_line);
}

Line::~Line(void) {
   cout << "Freeing memory!" << endl;
   delete ref_len;
}

int Line::getLength( void ) {
   return *ref_len;
}

void display(Line obj) {
   cout << "Length of line : " << obj.getLength() <<endl;
}


int main(){
    Line line_1(10);
    Line line_2 = Line (20);
    cout << "Trial 1" << endl;
    Line line_3 = line_1 + line_2;
    cout << "Trial 2" << endl;
    Line line_3_2 = line_3;
    return 0;
}