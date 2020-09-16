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

#### Startup Noise
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
##### InitializeICUDefaultLocation
We start with a call to `InitializeICUDefaultLocation` function. We see that it's in the v8::v8 scope. While we can find the function definition in the [/init/icu_util.h](), it doesn't tell us a whole lot about what it is. Few google searches after, it looks like [ICU](http://site.icu-project.org/) is a C++ library which is imported into V8 under [v8/third_party/icu](https://chromium.googlesource.com/v8/v8/+/69abb960c97606df99408e6869d66e014aa0fb51/DEPS#16). 

It is responsible for language localisation and textual support, timing and stuff. Yeah, not really interested.

##### InitializeExternalStartupData

This function is defined in `init/startup-data-util.h`. It seems to have to do with restoring contextual states for standalone executables like `d8`. I also seems to be looking for specific files like `snapshot_blob.bin`. Oh well, moving on.

##### platform
The `NewDefaultPlatform` is an interesting one. in V8's terminology, a platform defines interesting properties like 
* number of threads V8 process will be using
* tracing configuration
* stackdumping behaviour
* .... and other stuff

Once you have the platform initialized, it's like you have a v8 instance up. Now, you start with the actual things which start to connect with the actual JS processing and related stuff.

You then use this pointer to initialize the V8 engine.

#### Creating an Isolate & Context

Okay, now that we have our V8 running, let's get some terminologies which are essential going. Take a look at the code below:
```C++
 // Create a new Isolate and make it the current one.
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  {
    v8::Isolate::Scope isolate_scope(isolate);

    // Create a stack-allocated handle scope.
    v8::HandleScope handle_scope(isolate);

    // Create a new context.
    v8::Local<v8::Context> context = v8::Context::New(isolate);

    // Enter the context for compiling and running the hello world script.
    v8::Context::Scope context_scope(context);
    {// Another un-named namespace}
 }
```
##### Step 1: CreateParams

We start off by creating a [create_params](https://source.chromium.org/chromium/chromium/src/+/master:v8/include/v8.h;drc=b79862be255715191be99c3ee924acdea58526fd;bpv=1;bpt=1;l=8215). This is a stucture with corresponds to a V8 Isolate, and acts like a configuration variable for the Isolate.
Wait, what's isolate?

Think of __isolate__ as an independent flow of execution, containing copies of all the componenets required to execute JS code in it's entirety. Quoting the documentation:
> Isolate represents an isolated instance of the V8 engine.  V8 isolates have completely separate states.  Objects from one isolate must not be used in other isolates.  The embedder can create multiple isolates and use them in parallel in multiple threads.  An isolate can be entered by at most one thread at any given time.

Next, for our isolate we get a `NewDefaultAllocator`, which is a datamember of `CreateParams` structure and controls the heap allocator for the instance. We wouldn't want two seperate instances to share the alloactor.

Once we have these basic components, we initialize the __isolate__ with help of our `create_param` structure. At this point we have a complete set of V8's instance (an Isolate), which we can use.

##### Step 2: Scopes & Destructors (A love story)

If you're new to V8 and not that accustomed to C++, the curly braces right after __Isolate__'s initialization might look werid to you. These are un-named namespaces

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