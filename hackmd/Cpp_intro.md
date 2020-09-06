# Chromium C++ Primer for C++98 Folks
##### lvalues, rvalues, C++11/14 & Chromium({}, auto, constructor: var_name{}, yada, yada, yada ... )


## Introduction

C++ powers most of the V8 engine. All the memory memory management logic, Ignition Interpreter, Turbofan (Optimizing compiler) are written in C++. Thus, it's very much required for anyone beginning with V8 exploitation to brush up their C++ skills enough to understand V8's source code. 

While a lot of us have run into C++ at somepoint in our lives, if it wasn't in a recent production code (which isn't older than you are) there's a great chance you never ran into the new, expansive and a little different world of new features from `C++11/14`. At time of writing (3rd Sept, 2020), Chromium's most codebase is based in `C++11/14` and being actively migrated for `C++17` with a [target for 2021](https://chromium-cpp.appspot.com/). While there's a lot of change, the changes from C++11/14 and onwards are pretty incremental. However, there's quite a leap in new things from `C++98` to `C++11/14` as we'll see.

Wait, I don't need to read no `C++11/14`. I know C++:
* Templates
* Multiple Inheritences
* Namespaces
* .... another 10K stuff
 
I know that as back of my hand. I mean,

![](https://thumbs.gfycat.com/TerrificImpureHarrier-size_restricted.gif)

To my surprise, not as easy as I expected it to be. But becomes way trival once you have the correct readings.


## Motivation

The journey starts when I tried getting started looking into V8's source code. I started with [@danbev's great repo](https://github.com/danbev/learning-v8). I hit a roadblock once I started looking into [taggedimpl](https://github.com/danbev/learning-v8#taggedimpl) which is the basis of most memory structures that V8 uses. I cloned the repo, [pulled up the code](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/objects/tagged-impl.h;l=23;drc=09fd7c717c08af5aaf2df1e0fe732cbe0bb87f11). I see roughly this:
```C++=
template <HeapObjectReferenceType kRefType, typename StorageType>
class TaggedImpl {
 public:
  static_assert(std::is_same<StorageType, Address>::value ||
                    std::is_same<StorageType, Tagged_t>::value,
                "StorageType must be either Address or Tagged_t");
                
  static const bool kIsFull = sizeof(StorageType) == kSystemPointerSize;

  static const bool kCanBeWeak = kRefType == HeapObjectReferenceType::WEAK;

  constexpr TaggedImpl() : ptr_{} {}
  explicit constexpr TaggedImpl(StorageType ptr) : ptr_(ptr) {}

  // Make clang on Linux catch what MSVC complains about on Windows:
  operator bool() const = delete;

  template <typename U>
  constexpr bool operator==(TaggedImpl<kRefType, U> other) const {
    static_assert(
        std::is_same<U, Address>::value || std::is_same<U, Tagged_t>::value,
        "U must be either Address or Tagged_t");
    return static_cast<Tagged_t>(ptr_) == static_cast<Tagged_t>(other.ptr());
  }
// yada yada yada.....
```
In few words, below are the things I don't quite understand:
* Line 1: `HeapObjectReferenceType kRefType` what's with the fixed looking `enum` getting passed into the template.
* Line 4: `static_assert`, well that's something I've never seen before. I won't what that does.
* Line 12: `constexpr TaggedImpl() : ptr_{} {}`, what's this weird constructor which is initialized with curly braces? What is this `_ptr` thing?
* Line 23: `static_cast<>`, what's this weird cast?

Me by this point,

![](https://media1.tenor.com/images/634d6dae7a9d4eb56a108469a05f831d/tenor.gif?itemid=8139976)

I guess we need to go do some brushup on our C++ Skills. Upon more re-search, most of it turned out to be pretty simple C++11 stuff. We'll be discussing a little more things than what's required to answer the questions above.

If you're a smarty pants (@mckade) and already know what's going in these lines, feel free to skim, skip over and move to the next article. For the rest of us, let's get going. Here's a link you might want to [skim through](https://blog.petrzemek.net/2014/12/07/improving-cpp98-code-with-cpp11/) just to sanity check.


## Understanding the new parts of C++11/14

I refered articles and videos from all over the internet which I will link. 

Before I start, below are three recomendations (in order):

* [C++98 to C++11/14](https://blog.petrzemek.net/2014/12/07/improving-cpp98-code-with-cpp11/) -> Covers most of the ideas required to undestand the snippet above for anyone with C++98 experience.
* [A Tour of C++ (2nd Ed)](https://learning.oreilly.com/library/view/a-tour-of/9780134998053/), Chapters 2,3,4,5,6 -> Covers the main new features in new specs of C++ in good depth (for better full knowledge)
* (Optional) | A lot of people told me to read [Effective Modern C++](amazon.com/Effective-Modern-Specific-Ways-Improve/dp/1491903996), while finally proved very helpful here. I recommend everyone to read up on Chapters 1,2,3 & 5. 
* (Optional) | [The C++ Programming Language, Fourth Edition](https://learning.oreilly.com/library/view/the-c-programming/9780133522884/ch23.html#ch23) - Chapter 23

I highly recommend reading the first two readings before moving ahead. Now let's take a look at few new features and then we'll circle back to the things we initially didn't understand well.

### Rvalues-Lvalues, Rvalue References & Perfect Forwarding

Rvalues Lvalues have secretly existed since long in C++. While the definition of Rvalues and Lvalues is a little trickly, you can identify them as:
* **Rvalues**: Objects that usually have named aliases and live in memory.
* **Lvalues**: Temporary objects usually created for computation, assignment or return (usually unamed).

I agree that there are a ton of usually(s) here, but these are dependent on multiple things and is determined during compile time. To add to that, this can be cased with operators like `std::move` etc. So when in doubt, ask the compiler. 

I went ahead and expiremented different kinds of move scemantics. However, they are not strictly related to V8 and more of a general concept. If you're interested, checkout my experiments on [Move constructors and assignment operators](https://github.com/pranayga/expl0ring_V8/tree/master/Cpp/rvalue_references).

As far as knowledge for C++'s usage of these constructs is concerened, I recommend viewing these links (in order):

* The Cherno -> Channel with a great C++ playlist.
  * [lvalues and rvalues in C++](https://www.youtube.com/watch?v=fbYknr-HPYE) 
  * [Move Semantics in C++](https://www.youtube.com/watch?v=ehMg6zvXuMY)
  * [std::move and the Move Assignment Operator in C++](https://www.youtube.com/watch?v=OWNeCTd7yQE)
* [Chromium's Guide on C++ Rvalue references Usage](https://www.chromium.org/rvalue-references) -> Chromium's office guidelines on usage of Rvalue references.
* (Optional) | [An Effective C++11/14 Sampler](h)ttps://channel9.msdn.com/Events/GoingNative/2013/An-Effective-Cpp11-14-Sampler) - Scott Meyers
* (Optional) | [Perfect Forwarding](https://eli.thegreenplace.net/2014/perfect-forwarding-and-universal-references-in-c) --> It's one of the festures which are used less. TODO: Explore how it's used in V8

### Constructor{}, initilization lists
Initilization Lists are another intresting thing which were introduced in C++11. The initializer lists are used to directly initialize data members of a class (inline of the document). Example:
```C++
// Try Running on ccp.sh
#include <iostream>

using namespace std;

class Line {
    // An initializer list starts after the constructor name 
    // and its parameters. The list begins with a colon ( : )
    // and is followed by the list of variables that are to be
    // initialized
    
    public:
      int getLength( void ){ return ref_len; };
      // All of​ the variables are separated by a comma
      // with their values in curly brackets
      Line( int len ): ref_len{len} {cout << "Initializer List constructor called!" << endl;};
     
    private:
      int ref_len;
};

int main(){
    Line line_1(10);
    cout << line_1.getLength() << endl;
}
```

Basically, data_member{value} is a initialization shorthand. So much simpler than initially thought. The code above is equalent to:
```C++
List::List (int len){
    ref_len = len;
    cout << "Initializer List constructor called!" << endl;
}
```
More details are available at [Initialization Lists Intro](https://en.cppreference.com/w/cpp/language/constructor).

## Second Look at TaggedImpl
Let's repaste the code down here and do over it together.
```C++=
template <HeapObjectReferenceType kRefType, typename StorageType>
class TaggedImpl {
 public:
  static_assert(std::is_same<StorageType, Address>::value ||
                    std::is_same<StorageType, Tagged_t>::value,
                "StorageType must be either Address or Tagged_t");
                
  static const bool kIsFull = sizeof(StorageType) == kSystemPointerSize;

  static const bool kCanBeWeak = kRefType == HeapObjectReferenceType::WEAK;

  constexpr TaggedImpl() : ptr_{} {}
  explicit constexpr TaggedImpl(StorageType ptr) : ptr_(ptr) {}

  // Make clang on Linux catch what MSVC complains about on Windows:
  operator bool() const = delete;

  template <typename U>
  constexpr bool operator==(TaggedImpl<kRefType, U> other) const {
    static_assert(
        std::is_same<U, Address>::value || std::is_same<U, Tagged_t>::value,
        "U must be either Address or Tagged_t");
    return static_cast<Tagged_t>(ptr_) == static_cast<Tagged_t>(other.ptr());
  }
// yada yada yada.....
```
* Line 1: `HeapObjectReferenceType kRefType` what's with the fixed looking `enum` getting passed into the template.
> This kRefType in a Enum of `HeapObjectReferenceType` and defines if an object is weak or strong. It's part of the template class. That's nothing new.
* Line 4: `static_assert`, well that's something I've never seen before. I won't what that does.
> aaha! So, here we have a static_assert. These lines are computed by the compiler, and serves as ways to make sure that the templating, memory sizes and things like that are getting deduced as expected. Here it's used to make sure that the deduced template types match with the expected types for the == operator.
* Line 12: `constexpr TaggedImpl() : ptr_{} {}`, what's this weird constructor which is initialized with curly braces? What is this `_ptr` thing?
> Similar to static_assert (yet a little different), `constexpr` expressions are expressions which are expanded by the compiler **when possible**.This depends on various things, like if all the parameters support it (among others which you can [find here](https://blog.quasardb.net/2016/11/22/demystifying-constexpr).) Here, it is used to allow the comparison to happen during compile time!
>> This functions in a way very similar to MACROS, however here the compiler can deduce the result of the operation itself and place the result instead of say a call!. 
* Line 23: `static_cast<>`, what's this weird cast?
> Finally, we have a static_cast, which can be used to change the interpretation of an object during compilation. More [here](https://en.cppreference.com/w/cpp/language/static_cast).

Finally towards the [end of the class definition](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/objects/tagged-impl.h;l=183;drc=09fd7c717c08af5aaf2df1e0fe732cbe0bb87f11), we have:
```C++=183
StorageType ptr_;
```
If we consider the line 183 along with 12&13 (from above), we see that this class (among a ton of other functions), have two main constructors, one which accepts a `StorageType` and another which is empty

And it finally Makes sense!

![](https://images.gr-assets.com/hostedimages/1443173595ra/16329578.gif)

Looking back, now you'd be like Hmmm...

That was so obvious and easy. I agree! We didn't need to read 90% of the links in order to make sense of our initial function. However these articles gives us a wholistic understanding of the features and places to refer back to! 

Initially nothing made sense but after reading a few articles things fit in!

## Where to next?

Once you're good with the basics of `C++11/14`, I would recommend the following docs from chromium's & V8's devbase:
* [Embedding V8 101](https://v8.dev/docs/embed)
* [Chromium's Dev Home](https://www.chromium.org/developers)
* (TODO, out of scope)[Smart Pointers guide](https://www.chromium.org/developers/smart-pointer-guidelines)
* (TODO, out of scope)[Important Abstractions](https://www.chromium.org/developers/coding-style/important-abstractions-and-data-structures) 

## More References (In no perticular Order)

* [Pointers VS References](https://www3.ntu.edu.sg/home/ehchua/programming/cpp/cp4_PointerReference.html) -> Use this as a refresher for how references differ from pointers, what's legal and what's not. Also, mentions how const functions can accept non-const variables, but not the other way around.
* Rvalues & Lvalues
  * [Chromium's R-values Starter](https://www.chromium.org/rvalue-references)
  * [Lvalues & Rvalues](https://www.internalpointers.com/post/understanding-meaning-lvalues-and-rvalues-c) -> Reading this would tell you about the lvalues (things that exist in memory) and rvalues (which don't exist in memory). It also explains how operators at compiler level relate to R&Lvalues (along with example error messages).
  * [Inderstanding Rvalue References](http://thbecker.net/articles/rvalue_references/section_01.html)