// Reference : https://www.tutorialspoint.com/cplusplus/cpp_copy_constructor.htm#:~:text=The%20copy%20constructor%20is%20a,an%20argument%20to%20a%20function.
/**
 * This example shows how a move constructor based on Rvalue reference 
 *  can be implemented. And how it only impacts the rvalue reference.
 * Secondly, it reminds that assignment operators are different from 
 * constructors.
 */
#include <iostream>

using namespace std;

class Line {

   public:
      int getLength( void );
      int* getLengthPointer( void );
      Line( int len );                             // simple constructor
      Line( const Line &obj);                      // copy constructor
      Line( Line &&obj);                           // move constructor
      ~Line();                                     // destructor
      Line operator+(const Line &obj_b);           // + operator
      Line& operator= ( const Line & ) = default;	// trying default copy assignment

   private:
      int* ref_len;
};

// Member functions definitions including constructor
Line::Line(int len) {
   cout << "Normal constructor allocating ptr" << endl;
   
   // allocate memory for the pointer;
   ref_len = new int;
   cout << "Allocate space for " << len << " at:" << ref_len << endl;
   
   *ref_len = len;
}

Line::Line(const Line &obj) {
   ref_len = new int;
   cout << "Copy constructor copying " << *obj.ref_len <<", src_ptr:" << obj.ref_len << " dest_ptr:" << ref_len << endl;
   
   cout << "Allocate space for Copying " << *(obj.ref_len) << endl;
   *ref_len = *(obj.ref_len); // copy the value
}

Line::Line(Line &&obj) {
   cout << "Move constructor moving " << *obj.ref_len <<", src_ptr:" << obj.ref_len << endl;
   ref_len = obj.ref_len; // Movinf pointer
   obj.ref_len = NULL;
}

Line Line::operator+(const Line &obj_b){
    cout << "Inside + operator. this:" << *(this->ref_len) << " obj_b:" <<*(obj_b.ref_len) << endl;
    Line temp_line = Line(*(this->ref_len) + *(obj_b.ref_len));
    return (temp_line);
}

Line::~Line(void) {
   cout << "Freeing memory Address: " << ref_len << "!" << endl;
   delete ref_len;
}

int Line::getLength( void ) {
   return *ref_len;
}

int* Line::getLengthPointer( void ){
    return ref_len;
}

void display(Line obj) {
   cout << "Length of line : " << obj.getLength() <<endl;
}


int main(){
    Line line_1(10);
    Line line_2 = Line (20);
    cout << "========Trial diff 1==========" << endl;
    Line line_3 = line_1 + line_2;
    cout << "Trial 2" << endl;
    Line line_3_2 = line_3;
    cout << "========Trial diff 3==========" << endl;
    line_3_2 = line_2;
    cout << "line_3_2 is located at: " << line_3_2.getLengthPointer() << " Value: " << line_3_2.getLength() << endl;
    cout << "Eewww, dirtly shallow copy. I don't like JAVA style." << endl;
    return 0;
}