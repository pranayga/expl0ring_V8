# Exploring the V8 engine - I
##### This post start our exploration into the V8 sourceode. We use the [hello-world.cc](https://source.chromium.org/chromium/chromium/src/+/master:v8/samples/hello-world.cc) and the [embedding process](https://v8.dev/docs/embed) as our starting point and go exploirng from there.

## Introduction

V8's codebase is no small beast. It keeps getting updated [all the time](https://chromium-review.googlesource.com/q/project:v8%252Fv8+status:open) with faster and often changes which refine a huge chunck of internal structures. Our motivation in this post is to understand basics about the staple objects in V8's execution context alongside the basic control flow.

V8 provides you with some sample files inside [/sample](https://source.chromium.org/chromium/chromium/src/+/master:v8/samples/) and this post about [embedding v8](https://v8.dev/docs/embed). The idea of embedding V8 is to use it as an C++ API. The few included examples could provide us few important API functions to kickstart our exploration of the major important chuncks of V8.

:small_red_triangle:__NOTE__: The analysis below is provided from 
* commit hash: `7e8e76e784061277e13112c67c21c3f9438da257`
* [hello-world.cc](https://source.chromium.org/chromium/chromium/src/+/master:v8/samples/hello-world.cc;l=1;drc=7e8e76e784061277e13112c67c21c3f9438da257) - commit specific link

## Hello-Exlorer.cc

The [hello-world.cc](https://source.chromium.org/chromium/chromium/src/+/master:v8/samples/hello-world.cc) file is a simple example for embedding V8 in C++. The example involves simple executing a simple JS command: `"'Hello' + ', World!'"`

Let's start with examining the main chuncks of the file, go deeper and deeper. I would recommend you to open the file in another tab to view the entire file.

### Initializing V8
We see two v8 specific imports.
```C++
#include "include/libplatform/libplatform.h"
#include "include/v8.h"
```
The `include/libplatform/libplatform.h` is our library with base functions which help us star & control the V8 process in a more fine tuned manner. Things like how many threads to spawn, how long to wait for a execution to complete among others can be controlled using the API explosed here. This headerfile exposes the `v8::platform` namespace.

The `include/v8.h` is our base libary which contains most of the exported functions, namespaces, sub-namespaces. These functions enable us to create new __isolates__, __contexts__, __handles__....


Wait @pranayga, you didn't explain what __isolates__, __contexts__, __handles__ are! Yep, we'll get there in just a minute. Let's continue.

### Starting V8 Process

Now, Let's take a look at the first few lines of the example.

```C++
int main(int argc, char* argv[]) {
  // Initialize V8.
  v8::V8::InitializeICUDefaultLocation(argv[0]);
  v8::V8::InitializeExternalStartupData(argv[0]);
  std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform.get());
  v8::V8::Initialize();
  ....
 }
```

## Open Leads:

* [Embedding built-ins](https://v8.dev/docs/embed)
* CodeFlow
    * [Implementation of Closure](https://bugzilla.mozilla.org/show_bug.cgi?id=542071)
* [learning V8](https://github.com/danbev/learning-v8)
* [Pointer Compression - exploitation take](https://blog.infosectcbr.com.au/2020/02/pointer-compression-in-v8.html)
* Official Memory Related Docs
    * [Overall Structure references](https://goo.gl/Ph4CGz) 
    * [Pointer Compression](https://v8.dev/blog/pointer-compression)
* [V8 Closures from Blink's Prespective](https://chromium.googlesource.com/chromium/src/+/master/third_party/blink/renderer/bindings/core/v8/V8BindingDesign.md)