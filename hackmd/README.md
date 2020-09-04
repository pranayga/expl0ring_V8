# Week 1

## Introduction

Welcome to the inaguaral post for this series on V8 security auditing! We ([Pranay Garg](hashprks.com) and [John Johnson](https://m4dst4cks.github.io)) are excited to take a deep dive into everything that you need to get started in this space. Our goal is to give more people the opportunity to look into vulnerability research within Chromium's JavaScript engine. A lot of times, getting started in a new area comes with a lot of feelings like "I don't know what I don't know." There has been a lot of research in this space previously (see the massive list of references at the end), and it can be difficult to know which to read (and worse, what's outdated!). This first post will be an attempt to document what has been done before and give a very high-level summary of the current state of V8. In future posts we will also cover how to stay up to date with changes. I highly recommend skimming the links we provide so that the rest of the series makes sense. We believe that there is still a lot of research to be done in this area [and google does too](https://v8.dev/grant). While the V8/Chromium teams have put [countless security measures](https://www.chromium.org/Home/chromium-security/brag-sheet) in place, new exploitable bugs are found on a regular basis. We hope to explain the processes for vulnerability discovery and exploitation, as well as the code base, in a way that will allow more people to begin bug hunting and memory safety research. This week we will get started by covering several topics on JavaScript engines, C++, and basic high-level architecture choices of V8. Apologies in advance for a lot of "go read this," future posts will try to introduce original material.

A few quick notes if you are completely new to V8. V8 is the JavaScript engine for the Chromium browser, the open-source baseline for Chrome. It is also open-source, meaning that we can view all of the code, commit history, etc. There are several components within V8, but it's overall goal is to correctly, quickly, and securely execute JavaScript and WebAssembly code. You can read more about it in the [docs](https://v8.dev/).

## C++ things
The book we'll be referencing: [effective-modern-c](https://learning.oreilly.com/library/view/effective-modern-c/9781491908419/)

### Refreshers
- [pointers VS references](https://www3.ntu.edu.sg/home/ehchua/programming/cpp/cp4_PointerReference.html) -> Use this as a refresher for how references differ from pointers, what's legal and what's not. Also, mentions how const functions can accept non-const variables, but not the other way around.
- [Lvalues & Rvalues](https://www.internalpointers.com/post/understanding-meaning-lvalues-and-rvalues-c) -> Reading this would tell you about the lvalues (things that exist in memory) and rvalues (which don't exist in memory). It also explains how operators at compiler level relate to R&Lvalues (along with example error messages).
- [universal References](https://medium.com/pranayaggarwal25/universal-reference-perfect-forwarding-5664514cacf9), [understanding basics](http://thbecker.net/articles/rvalue_references/section_01.html)

### New topics

#### type deduction

Historically C++98 has had only one type deduction systems. However, C++11/14 introduces two new systems, one for auto and one for decltype. C++14 then extends the usage contexts in which auto and decltype may be employed. [[reference]:"Effective Modern C++"](https://learning.oreilly.com/library/view/effective-modern-c/9781491908419/ch01.html#deducing_types) 

/*:
    Add content for the chapter once you read it fully. @hashprks 
*/
#### namespaces

#### meta programming
 
## V8 in Chromium

V8 itself is a relatively small piece within the Chromium broswer. In fact, it is a completely separate code base that can be embedded within other projects (like NodeJS!). It is important to understand the layout here because we will talk a lot about V8 exploitation, but V8 is usually sandboxed within another process. V8 bugs must be chained with additional exploits to reach full blown code execution on most systems. For our purposes, we will be happy to gain unrestricted code execution within the V8 process.

![Credit: https://www.chromium.org/developers/how-tos/getting-around-the-chrome-source-code/Content.png](https://www.chromium.org/developers/how-tos/getting-around-the-chrome-source-code/Content.png)

The cool thing about V8 being so modular is that we do not have to understand anything about Chromium to understand V8. For example, V8 does it's own memory allocation, so we do not need to look at another code base to figure out how objects will look on the heap (besides the 3rd party allocators, of course).


## V8's Components

![Credit: https://medium.com/dailyjs/understanding-v8s-bytecode-317d46c94775](https://i.imgur.com/VXnZv8g.png)


Next, we'll explore some of the most important parts of V8. You can find all of the code [here](https://chromium.googlesource.com/v8/v8.git/+/refs/heads/master) or at the [GitHub mirror](https://github.com/v8/v8). In our next post we will go in-depth as to how these components are written in C++, but for now let's just get an understanding of what these things do. 

To understand the way V8 is segmented, you have to look at how modern web browsers run JavaScript. Although it is an interpreted language, JS engines often compile this code into architecture-specific machine code in a process known as Just-in-Time (JIT) compilation. Normally, an interpreted script will be converted into bytecode, which can be run architecture-independent. However, this code runs very slowly as all of the instructions need to be transformed from an intermeddiate language into the instructions for the current CPU. Alternatively, compiled code is very fast, but it requires some up-front cost of compiling the source into an executable. The idea for JS engines is to use JIT to get the best of both worlds. They use an interpreter to start running JavaScript immediately. However, if the engine detects that some code is being run often, then it will compile just that section of code with several optimizations. The way that this whole process works in V8 is best explained [here](https://ponyfoo.com/articles/an-introduction-to-speculative-optimization-in-v8).

#### Ignition

[Ignition](https://v8.dev/docs/ignition) is V8's interpreter. Many exploits focus on JIT code and the mistakes made during the compilation process. However, there was an article 


https://labs.bluefrostsecurity.de/blog/2019/04/29/dont-follow-the-masses-bug-hunting-in-javascript-engines/
https://docs.google.com/document/d/11T2CRex9hXxoJwbYqVQ32yIPMh0uouUZLdyrtmMoL44/edit?ts=56f27d9d#heading=h.6jz9dj3bnr8t

#### Turbofan

Turbofan has officially taken over as V8's compiler, even though some resources may show references to Crankshaft (replaced in 2018 :'( ). Most V8 exploits focus on this component, and as a consequence we will too. Some aspects to understand here will be the optimization steps, variable typing, and memory safety checks.

https://docs.google.com/presentation/d/1UXR1H2elTdAYJJ0Eed7lUctCVUserav9sAYSidxp8YE/edit#slide=id.g2fa9cbdadc_0_0

#### Liftoff



https://v8.dev/blog/liftoff

## Torque and the CodeStubAssembler (CSA)

[Torque](https://v8.dev/docs/torque) To get _even better_ performance, V8 comes with pre-compiled code for certain oft-used functions.

https://www.elttam.com/blog/simple-bugs-with-complex-exploits/#references

https://github.com/v8/v8/commit/a3353da8461cb95c9112aa854da8274fcaf211fb#diff-728022ff6df143aa254f88a0c51217c3

https://news.ycombinator.com/item?id=20540407

## sea of nodes


## ECMAScript

## Looking Forward

Next we'll do a little less reading (just the source code) and a lot more doing!


## Previous Work

Our goal in this study will be to create as complete a guide as possible to understanding the current state of V8 exploitation, and also it's future! However, it would be entirely impossible without the work that has already been accomplished that gave us our own starting point. Here are several references that we used for understanding. There are probably more that we could have listed, but these were the most influential. Again, we hope that this guide will have all of the information you needhis section is for credit to other researchers and a list of places to look for extra information.

#### Getting Started

https://codeburst.io/node-js-v8-internals-an-illustrative-primer-83766e983bf6

https://doar-e.github.io/blog/2019/01/28/introduction-to-turbofan/
https://doar-e.github.io/presentations/typhooncon2019/AttackingTurboFan_TyphoonCon_2019.pdf
https://docs.google.com/presentation/d/1DJcWByz11jLoQyNhmOvkZSrkgcVhllIlCHmal1tGzaw/edit#slide=id.p
https://sensepost.com/blog/2020/intro-to-chromes-v8-from-an-exploit-development-angle/
https://blog.appsignal.com/2020/07/01/a-deep-dive-into-v8.html
https://github.com/danbev/learning-v8
https://github.com/push0ebp/v8-starter-guide
http://eternalsakura13.com/2018/05/06/v8/
[An Introduction to Speculative Optimization in V8 by Benedikt Meurer](https://ponyfoo.com/articles/an-introduction-to-speculative-optimization-in-v8)

#### Development Perspective

[V8 Docs](https://v8.dev/docs/)
[Intermediate Representation](https://docs.google.com/presentation/d/1Z9iIHojKDrXvZ27gRX51UxHD-bKf1QcPzSijntpMJBM/edit#slide=id.g19134d40cb_0_502)
[Turbofan Design](https://docs.google.com/presentation/d/1sOEF4MlF7LeO7uq-uThJSulJlTh--wgLeaVibsbb3tc/edit#slide=id.g5499b9c42_01170)
[JavaScript Engine Internals by Mathias Bynens](https://www.youtube.com/watch?v=-lt6a9kbc_k)
[JavaScript Engine Fundamentals by Mathias Bynens](https://mathiasbynens.be/notes/shapes-ics)
http://eternalsakura13.com/2018/06/16/nodefest_v8/
https://jayconrod.com/posts/52/a-tour-of-v8-object-representation

#### Exploring Code

[Embedding V8](https://v8.dev/docs/embed)
[C++ Code guide](https://www.chromium.org/developers/cpp-in-chromium-101-codelab)

#### Memory structure

https://deepu.tech/memory-management-in-v8/
https://www.fullstackacademy.com/tech-talks/memory-management-js-vs-c-and-understanding-v8

#### Writing V8 C++ plugins
https://www.freecodecamp.org/news/understanding-the-core-of-nodejs-the-powerful-chrome-v8-engine-79e7eb8af964/
https://explorerplusplus.com/blog/2019/03/07/embedding-v8-c++-application
https://dustinoprea.com/2018/09/26/c-embedding-the-v8-javascript-engine/
https://nicedoc.io/pmed/v8pp

#### Exploitation 

https://github.com/vngkv123/aSiagaming/blob/master/Chrome-v8-tutorials/README.md
http://eternalsakura13.com/2018/08/02/v8_debug/

#### Specific Bugs

https://doar-e.github.io/blog/2019/05/09/circumventing-chromes-hardening-of-typer-bugs/
https://bugs.chromium.org/p/chromium/issues/detail?id=762874
https://blog.exodusintel.com/2019/04/03/a-window-of-opportunity/
https://blog.exodusintel.com/2019/09/09/patch-gapping-chrome/
https://blog.exodusintel.com/2020/02/24/a-eulogy-for-patch-gapping/
https://sensepost.com/blog/2020/the-hunt-for-chromium-issue-1072171/
https://github.com/vngkv123/aSiagaming/blob/master/Chrome-v8-906043/Chrome%20V8%20-%20-CVE-2019-5782%20Tianfu%20Cup%20Qihoo%20360%20S0rrymybad-%20-ENG-.pdf
https://www.elttam.com/blog/simple-bugs-with-complex-exploits/#references

#### CTF Problems

https://dmxcsnsbh.github.io/2020/07/20/0CTF-TCTF-2020-Chromium-series-challenge/
https://syedfarazabrar.com/2019-12-13-starctf-oob-v8-indepth/
https://gts3.org/2019/turbofan-BCE-exploit.html
https://www.jaybosamiya.com/blog/2019/01/02/krautflare/
https://abiondo.me/2019/01/02/exploiting-math-expm1-v8/

#### General JIT Compiler Exploitation

[Attacking JavaScript Engines by Saelo](http://www.phrack.org/papers/attacking_javascript_engines.html)
[Exploiting Logic Bugs in JavaScript JIT Engine by Saelo](http://phrack.org/papers/jit_exploitation.html)
[Blackhat Presentation by Saelo](https://saelo.github.io/presentations/blackhat_us_18_attacking_client_side_jit_compilers.pdf)
[JITsploitation Series by Saelo](https://googleprojectzero.blogspot.com/2020/09/jitsploitation-one.html)

## References

https://medium.com/dailyjs/understanding-v8s-bytecode-317d46c94775
https://ponyfoo.com/articles/an-introduction-to-speculative-optimization-in-v8







https://zon8.re/posts/v8-chrome-architecture-reading-list-for-vulnerability-researchers/