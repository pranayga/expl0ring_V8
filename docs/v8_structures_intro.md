# Starting the V8 engine - I
##### This post start our exploration into the V8 sourceode. We use the [hello-world.cc](https://source.chromium.org/chromium/chromium/src/+/master:v8/samples/hello-world.cc) and the [embedding process](https://v8.dev/docs/embed) as our starting point and go exploirng from there.

## Introduction

V8's codebase is no small beast. It keeps getting updated [all the time](https://chromium-review.googlesource.com/q/project:v8%252Fv8+status:open) with faster and often changes which redefine a huge chunck of internal structures. Our motivation in this post is to understand basics about the staple objects in V8's execution alongside the basic control flow.

V8 provides you with some sample files inside [/sample](https://source.chromium.org/chromium/chromium/src/+/master:v8/samples/) and this post about [embedding v8](https://v8.dev/docs/embed). The idea of embedding V8 is to use it as an C++ API. The few included examples could provide us few important API functions to kickstart our exploration of the major important chuncks of V8.

## Hello-Explorer.cc


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