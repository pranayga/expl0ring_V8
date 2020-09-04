# Chromium C++
##### lvalues, rvalues, C++11/14 & Chromium(... and some rvalue references)

## Introduction
C++ powers most of the V8 engine. All the memory memory management logic, Ignition Interpreter, Turbofan (Optimizing compiler) are written in C++. Thus, it's very much required for anyone beginning with V8 exploitation to brush up their C++ skills enough to understand V8's source code. 

While a lot of us have run into C++ at somepoint in our lives, if it wasn't in a production environment there's a great chance you never ran into the awesome and expansive world of new festures from `C++11/14`. At time of writing (3rd Sept, 2020), Chromium's most codebase is based in `C++11/14` and being actively migrated for `C++17` with a [target for 2021](https://chromium-cpp.appspot.com/). While there's a lot of change, the changes from C++11/14 and onwards are pretty incremental. However, there's quite a leap in new things from `C++98` to `C++11/14` as we'll see.

Wait, I don't need to read no `C++11/14`. I know C++:
* Templates
* Multiple Inheritences
* Namespaces
* .... another 10K stuff
 
I know that as back of my hand. I mean,
![](https://thumbs.gfycat.com/TerrificImpureHarrier-size_restricted.gif)

To my surprise, not as easy as I expected it to be.

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

I guess we need to go do some brushup on our C++ Skills. If you're a smarty pants and already know what's going in these lines, feel free to skip over and move to the next article. For the rest of us,let's get going.

## Understanding the new parts of C++11/14
For most of article below, I refered articles and videos from all over the internet which I will link. Before I started, a lot of people told me to read [Effective Modern C++](amazon.com/Effective-Modern-Specific-Ways-Improve/dp/1491903996), while finally proved very helpful here. I recommend everyone to read up on Chapters 1,2,3 & 5. Now let's take a look at few new features and then we'll circle back to the things we initially didn't understand well.

### Understanding Template Type deduction
#### Rvalues-Lvalues & Rvalue References


## Second Look at TaggedImpl
// TODO

## References
- [Chromium  C++ 101](https://www.chromium.org/developers/cpp-in-chromium-101-codelab)
- [Chromium Public API](https://www.chromium.org/blink/public-c-api)
- [Chromium's R-values Starter](https://www.chromium.org/rvalue-references)
- [pointers VS references](https://www3.ntu.edu.sg/home/ehchua/programming/cpp/cp4_PointerReference.html) -> Use this as a refresher for how references differ from pointers, what's legal and what's not. Also, mentions how const functions can accept non-const variables, but not the other way around.
- [Lvalues & Rvalues](https://www.internalpointers.com/post/understanding-meaning-lvalues-and-rvalues-c) -> Reading this would tell you about the lvalues (things that exist in memory) and rvalues (which don't exist in memory). It also explains how operators at compiler level relate to R&Lvalues (along with example error messages).
- [understanding Rvalue References](http://thbecker.net/articles/rvalue_references/section_01.html)