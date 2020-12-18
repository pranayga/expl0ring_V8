# V8 Internals - A Security Nerd's Perspective

## Abstract
Open-source software is often passively trusted by users and 
developers. This is especially true for modern web-browsers 
like Chromium, which touches a user's personal data very frequently. However, browsers are hard to secure, inherently complex machines. Most browsers have their own memory management and system software, almost like an operating system. In this study, we present the internals of the V8 engine, 
which is the JavaScript engine powering the Chromium web-browser. 
We analyze the major components in-depth and then dive into 
taint-tracking implementation / previous vulnerabilities in 
order to explore the current landscape systematically. 
We intend to provide a one-stop location to understand the 
core V8 internals, while looking for opportunities for 
improvement from a security perspective. 

## Introduction
The twenty-first century can be thought of as a century of 
open-source software. From industrial systems all the way to your microwave, systems might be running a version of open-source software. While we use a lot of this software indirectly, web browsers are a unique breed. From an application perspective, they allow us to interact with the world and do anything from sending an email, all the way to stream videos and games via the cloud. On the software end, 
Chromium does its own memory management, has a builtin interpreter, and compiler which generates code. Chromium has 
characteristics of application software and system software all at once.

We trust our web browser with all of our personal data. But where does this trust come from? Since it is open-source, it is 
important that some formal verification goes into the system<sup>[1]</sup>. Google manages this responsibility 
via their bug bounty system<sup>[2]</sup> and through various other means. 
However, Chromium's codebase changes significantly over time,
hence we must keep the barrier to entry low enough 
so that new researchers find it easy to get started with 
Chromium and improve on its security.

The goal of this study is to make such contributions to a 
component of Chromium, specifically its JavaScript engine, V8. 
The V8 engine is also used in NodeJS, to run JavaScript code on the server-side, as well as other standalone application frameworks,
such as electron. This causes security bugs in V8 to have a much larger blast radius when something does go wrong. 

V8 has a steep learning curve for anyone who has little 
experience working on the Chromium codebase. While documentation 
exists for developers, there is little documentation for 
anyone looking from a security perspective. There has been 
some notable work by Sergei Glazunov<sup>[9]</sup>, Stephen 
Röttger<sup>[3]</sup>, Jeremy Fetiveau<sup>[4]</sup>, Samuel 
Groß<sup>[5]</sup>, and Javier Jimenez<sup>[6][7]</sup>, who 
all have published blog posts on their findings. There have 
also been several fuzzing projects<sup>[8]</sup> introduced, 
with enough documentation so that anyone can use those 
tools. However, these resources do not give someone who is 
new to the field enough information to begin their research, 
and while fuzzing can cover more code in a faster time, it 
is no replacement for code review and documentation on how 
the systems work internally.

We will start by exploring some [background](#background) 
work which is required to understand the V8 engine better. 
This will mainly consist of some newer C++11/14 features which chromium codebase makes us of, and a tour of V8. 
Next, we shall explore the internals in a sequential and easy to follow manner, which would give you a tour of the components inside V8.

## Background

The V8 and Chromium codebases are written using the C++14 standard at the time of writing. V8 exploits a lot of newer C++14 features
like `const-expr`, `auto` among others to write code in a very extensible way. Ideas relating to this have been covered in the 
[C++ Intro](docs/cpp_intro.md) article.

V8 is a JavaScript optimizing compiler. It has an interpreter 
and an optimizing compiler, which can collect type information while 
JavaScript is running and produce more specialized and efficient code. 
The document [high level tour](docs/high_level_architecture.md) goes over the major components inside V8 to give you a summary of the components inside V8.

The remainder of the report is divided into several sections. [Internals](#internals) 
provides a view of the major components in V8 without any assumed prior knowledge. Following that, [Experiements](#experiments) discusses various experiments and hands-on activities we performed to understand one or more components in greater depth. Lastly, we present an analysis of aversion of V8 which was modified to implement taint-tracking `String` objects. 

## Internals

In this section, we will start looking into various major components
inside the V8 engine.

### V8 Codebase
In the [V8 codebase exploration](docs/V8_code_base.md) article, we go over 
the location where the codebase for the components we learned about in the 
[high level tour](/docs/high_level_architecture.md) are located. Some new
components like `codegen` and `execution` are also introduced, which act 
as entities which makes V8 work together well.

### Turbofan

In the [Optimizing Compiler - Turbofan](docs/Turbofan.md) article, we explore 
Turbofan - "The optimizing compiler inside V8". This article goes in depth on how the optimizing compiler works, the `Sea of Nodes`, and `Typing`. It also describes `turbolizer` which is a tool that can be used to analyze Javascript code which is getting optimized by Turbofan, as well as other debugging techniques.

### JavaScript Variables' Representation in V8

In the [JavaScript Objects in memory](/docs/JavaScript%20Variables'%20Representation%20in%20Memory.md)
article, we go over how V8's memory management and object creation backend functions to create JavaScript objects. Key ideas, such as how JavaScript functions look like in C++ memory, `Pointer Compression`,
`Shapes & Hidden Classes`, `inline caches` are discussed here. This 
information will prove to be helpful in understanding how any basic 
JavaScript object (like `String`) is implemented in the V8 engine.

### CSA, Torque & Builtins
In the [Builtins](docs/builtins_basics.md) article, we explore how the 
ECMAScript standard is implemented in V8 (builtins). Since V8 needs to produce 
bytecode, which is platform-dependent, a lot of architecture-specific tweaks
are possible to boost performance. Rather than handcrafting builtins for
each of the many platforms, V8 built a higher level assembler, which 
allows us to write the builtins in near assembly and then compiles it
down to an architecture-optimized version.

## Experiments
While in the [Internals](#internals) section we read through great details
about the major components of V8, nothing compares to a hands-on 
experience playing around with the components.

### Embedding V8 - Tracing Control Flow
In this two part series, we explore how the idea of Embedding V8 works.
In the [V8 exploration - I](docs/v8_exploration_I.md), we look at the basics of embedding V8 inside another program. This is the technique that is used by `Chromium`, `NodeJS`, and others to interface with V8 invisibly.

In [V8 exploration - II](docs/v8_exploration_II.md), we try executing a simple line of JavaScript code and tracing it through the V8 codebase. This helps us to get an intuitive understanding of the locations which are hit by the V8 codebase when the interesting functions are called. We also discuss how to effectively use GDB while debugging V8.

### JavaScript Engine Exploitation Primitives
In the [V8 Exploitation Primitives](/docs/JavaScript%20Engine%20Exploitation%20Primitives.md)
article, we shift gears and talk about the exploitation primitives which have existed in the V8 engine subspace. Here we discuss the primitives that we want to gain through a bug, allowing us to gain different kinds
of privileges. This is done through the exploration of various examples.

## Taint - Tracking in V8
Browsers touch data originating from a lot of sources. Often webpages have differentiating and unusual 
characteristics when it comes to how user data touches various components of a webpage. This can be effectively 
utilized to detect and track certain kinds of behaviors often exhibited during exploitation. However, to achieve 
this, we need to track the flow of this tainted information. This requires support at the platform level.
For browsers like chromium, taint tracking can help us detect vulnerabilities like XSS, which was demonstrated by a 
paper from cylab<sup>[10]</sup>. However, chromium doesn't provide any native taint tracking support. Hence the 
researchers went and customized the chromium build to support taint tracking. This was not a trivial task. Having a 
good grasp of the major components inside V8 is required to make the numerous changes required to incorporate such 
functionality into V8.

Thus in the following posts, we shall look into the efforts which were made and explore the actual 
[source code](https://cement.andrew.cmu.edu/stroucki/chromium-taint-tracking) modification which allows 
taint tracking, with a special emphasis on V8-based modifications.

### Tweaking / Adding a new Object type to V8 - (In works)
Before we start propagating any kind of taint data, we need to figure out how to store that taint data. Taint data can be maintained at different granularity levels.  In the article [taints in V8](docs/tweaking_v8_objects.md), we explore one possible way of modifying the base String class to incorporate taint data.

### Propagating Taints in V8  - (In works) 
Once we have the idea of a changed String class, the next step would be to accommodate the actual taint propagation inside V8 during the interaction between different string objects. In the post [taint_tracking_stroucki](docs/taint_tracking_stroucki.md), we explore how taints initially get introduced from blink and then look at the builtins which are responsible for propagating the taint as different objects interact.

Once we have some idea of the locations where taint is propagated, next would be to look at how exactly taint propagation functions. This is where `taint_tacking` library which was custom built for the very task comes in. the post [taint_tracking_stroucki_II](docs/taint_tracking_stroucki_II.md), discusses the codebase which is actually responsible for the taint management. This is the library that is triggered by the builtins, each time we need to propagate any taint data.

This is total completes a high-level overview of the code inside V8, which manages and propagates our taint data.

### Propogating Taint in Blink & though V8 - (In works) 
Once we have a working taint propagation system in V8, the next point of attack would be to actually generate the initial points of taint and mark the sinks. In the post [taint_tracking_stroucki_III](docs/taint_tracking_stroucki_III.md), we discuss how we configure blink to mark the taint sources and sinks alongside enable taint propagation defined in the blink specific functions.

## Exploiting a V8 N-Day
We have 2 articles to explain a V8 bug from 2020; including how it was found, fully understanding it,
and actually exploiting it. We used this as an opportunity to get some initial exploration
in exploit writing for V8. This will lead to our final topic from the bug research side. The
[initial look](/docs/Exploring%20Bug%201051017%20in%20V8.md) describes the bug and its fixes and the 
second article contains the [exploit walkthrough](/docs/Exploiting%20Bug%201051017.md).

## References
[1] M. Curphey and D. A. Wheeler, “Improving Trust and Security in Open Source Projects,” p. 27.

[2] ‘Chrome Rewards – Application Security – Google’. https://www.google.com/about/appsecurity/chrome-rewards/index.html (accessed Dec. 03, 2020).


[3] Ben, ‘Project Zero: Trashing the Flow of Data’, Project Zero, May 10, 2019. https://googleprojectzero.blogspot.com/2019/05/trashing-flow-of-data.html (accessed Aug. 30, 2020).

[4] ‘Diary of a reverse-engineer - Jeremy “__x86” Fetiveau’. https://doar-e.github.io/author/jeremy-__x86-fetiveau.html (accessed Aug. 30, 2020).

[5] Tim, ‘Project Zero: JSC Exploits’, Project Zero, Aug. 29, 2019. https://googleprojectzero.blogspot.com/2019/08/jsc-exploits.html (accessed Aug. 30, 2020).

[6] ‘SensePost | Intro to chrome’s v8 from an exploit development angle’. https://sensepost.com/blog/2020/intro-to-chromes-v8-from-an-exploit-development-angle/ (accessed Aug. 25, 2020).

[7] ‘SensePost | The hunt for chromium issue 1072171’. https://sensepost.com/blog/2020/the-hunt-for-chromium-issue-1072171/ (accessed Aug. 30, 2020).

[8] ‘v8/test/fuzzer’, GitHub. https://github.com/v8/v8/tree/master/test/fuzzer (accessed Aug. 30, 2020).

[9] A Tale of Two Pwnies (Part 1)’, Chromium Blog. https://blog.chromium.org/2012/05/tale-of-two-pwnies-part-1.html (accessed Aug. 30, 2020).

[10] L. Bauer, S. Cai, and L. Jia ‘Run-time Monitoring and Formal Analysis of Information Flows in Chromium – NDSS Symposium’. https://www.ndss-symposium.org/ndss2015/ndss-2015-programme/run-time-monitoring-and-formal-analysis-information-flows-chromium/ (accessed Dec. 07, 2020).
