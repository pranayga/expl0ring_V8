# Exploring the V8 engine - I
##### This post starts our exploration into the V8 sourcecode. We use the [hello-world.cc](https://source.chromium.org/chromium/chromium/src/+/master:v8/samples/hello-world.cc) and the [embedding process](https://v8.dev/docs/embed) as our starting point and go exploring from there.

## Introduction

V8's codebase is no small beast. It keeps getting updated [all the time](https://chromium-review.googlesource.com/q/project:v8%252Fv8+status:open) with faster and often changes which refine a huge chunk of internal structures. Our motivation in this post is to understand the basics about the staple objects in V8's execution context alongside the basic control flow.

V8 provides you with some sample files inside [/sample](https://source.chromium.org/chromium/chromium/src/+/master:v8/samples/) and this post about [embedding v8](https://v8.dev/docs/embed). The idea of embedding V8 is to use it as a C++ API. The few included examples could provide us a few important API functions to kickstart our exploration of the major important chunks of V8.

:small_red_triangle:__NOTE__: The analysis below is provided from 
* commit hash: `7e8e76e784061277e13112c67c21c3f9438da257`
* [hello-world.cc](https://source.chromium.org/chromium/chromium/src/+/master:v8/samples/hello-world.cc;l=1;drc=7e8e76e784061277e13112c67c21c3f9438da257) - commit specific link

![Let's get started](https://media1.tenor.com/images/7dcc0b5a2c64d741b6edd12a88738cf9/tenor.gif?itemid=4767352 "let'sgetstarted")

## Hello-Exlorer.cc

The [hello-world.cc](https://source.chromium.org/chromium/chromium/src/+/master:v8/samples/hello-world.cc) file is a simple example for embedding V8 in C++. The example involves simple executing a simple JS command: `"'Hello' + ', World!'"`

Let's start with examining the main chunks of the file, go deeper and deeper. I would recommend you to open the file in another tab to view the entire file.

### Initializing V8
We see two v8 specific imports.
```C++
#include "include/libplatform/libplatform.h"
#include "include/v8.h"
```
The `include/libplatform/libplatform.h` is our library with base functions which help us star & control the V8 process in a more fine-tuned manner. Things like how many threads to spawn, how long to wait for execution to complete among others can be controlled using the API exposed here. This header file exposes the `v8::platform` namespace.

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

It is responsible for language localization and textual support, timing, and stuff. Yeah, not interested.

##### InitializeExternalStartupData

This function is defined in `init/startup-data-util.h`. It seems to have to do with restoring contextual states for standalone executables like `d8`. I also seem to be looking for specific files like `snapshot_blob.bin`. Oh well, moving on.

##### platform
The `NewDefaultPlatform` is an interesting one. in V8's terminology, a platform defines interesting properties like 
* number of threads V8 process will be using
* tracing configuration
* stack dumping behavior
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

We start by creating a [create_params](https://source.chromium.org/chromium/chromium/src/+/master:v8/include/v8.h;drc=b79862be255715191be99c3ee924acdea58526fd;bpv=1;bpt=1;l=8215). This is a structure with corresponds to a V8 Isolate, and acts as a configuration variable for the Isolate.
Wait, what's isolate?

Think of __isolate__ as an independent flow of execution, containing copies of all the components required to execute JS code in its entirety. Quoting the documentation:
> Isolate represents an isolated instance of the V8 engine.  V8 isolates have completely separate states.  Objects from one isolate must not be used in other isolates.  The embedder can create multiple isolates and use them in parallel in multiple threads.  An isolate can be entered by at most one thread at any given time.

Next, for our isolate, we get a `NewDefaultAllocator`, which is a data member of `CreateParams` structure and controls the heap allocator for the instance. We wouldn't want two separate instances to share the allocator.

Once we have these basic components, we initialize the __isolate__ with help of our `create_param` structure. At this point, we have a complete set of V8's instance (an Isolate), which we can use.

##### Step 2: Scopes & Destructors (A love story)

If you're new to V8 and not that accustomed to C++, the curly braces right after __Isolate__'s initialization might look weird to you. These are [C++'s scopes](https://en.cppreference.com/w/cpp/language/scope). It tells the compiler that we're done with all the objects created inside the scope and to get rid of them once they go out of scope (ie: call destructors). This allows us to execute our example without having to manually free things.

##### Step 3: Setting __isolate_scope__ & creating __handle_scope__

Next, we configure our V8's scope to the new _isolate_ we just created, this tells our stack-allocated classes which _isolate_ they belong to.

:cyclone:
___handle_scope___ which is an instance of class __HandleScope__ is something which is especially interesting. Looking at the [source](https://source.chromium.org/chromium/chromium/src/+/master:v8/include/v8.h;drc=6d68750fbce8f7a7b8046de6a8f4a75e5648bb38;bpv=1;bpt=1;l=1201) for this file, I have wrappers for functions like `new[]`, `Delete[]`, however they're private. So it looks like a wrapper. At a closer inspection we a friend class [__Local__](https://source.chromium.org/chromium/chromium/src/+/master:v8/include/v8.h;l=194;drc=6d68750fbce8f7a7b8046de6a8f4a75e5648bb38;bpv=1;bpt=1).

###### :mega: __Local__ Class

This class looks pretty important, so let's stop for a second and take a deeper look. Reading the description on top of the class definition in `/include/v8.h`. 
While it might not make a ton of sense, for now, this is the first point where we see the garbage collection is mentioned. We'll come back to this class once we start looking more into the way V8 manages the data structs.

![Garbage Collector!](https://media1.tenor.com/images/560c23f7270834759f756243088f58cf/tenor.gif?itemid=12508431)

Okay, let's get back to our example

##### Step 4: Creating a V8 __context__ & exec :ship: 

Okay, next few steps are simple, we create a JS context inside out __isolate__ and just call the functions required to create out JS command and send it off for execution. Done.

![Now wait a minute](https://media1.tenor.com/images/4805b3613bd6e7f744e52bab4e98a5a0/tenor.gif?itemid=148506155 "explain that")

Wait, @pranayga. What is __context__ inside the __isolate__? Sorry, yeah right. About that.

We have created a V8 __isolate__, which is an independent set of all the components required to execute JS code. Now imagine you are executing JS files. You would want each file to execute individually, that is with its global context. The V8's [embeding doc](https://v8.dev/docs/embed#contexts) explains it quite succinctly:
> In V8, a context is an execution environment that allows separate, unrelated, JavaScript applications to run in a single instance of V8. You must explicitly specify the context in which you want any JavaScript code to be run.

:cyclone: I highly recommend reading two things now:
- Header file where [Context](https://source.chromium.org/chromium/chromium/src/+/master:v8/include/v8.h;drc=6d68750fbce8f7a7b8046de6a8f4a75e5648bb38;bpv=1;bpt=1;l=10291) is defined.
- Official guide to [embedding V8](https://v8.dev/docs/embed)


#### UP Next
In the next post, we'll be looking into the internal control flow starting at the point where the JS code is passed into the V8's context to be executed.

## Intersting Links:

* [Embedding built-ins](https://v8.dev/docs/embed)
* CodeFlow
    * [Implementation of Closure](https://bugzilla.mozilla.org/show_bug.cgi?id=542071)
* [learning V8](https://github.com/danbev/learning-v8)
* [Pointer Compression - exploitation take](https://blog.infosectcbr.com.au/2020/02/pointer-compression-in-v8.html)
* Official Memory Related Docs
    * [Overall Structure references](https://goo.gl/Ph4CGz) 
    * [Pointer Compression](https://v8.dev/blog/pointer-compression)
* [V8 Closures from Blink's Prespective](https://chromium.googlesource.com/chromium/src/+/master/third_party/blink/renderer/bindings/core/v8/V8BindingDesign.md)