# Introduction and Background

## Motivation

Our goal is to give more people the opportunity to look into vulnerability research within Chromium's JavaScript engine. Many times, learning a new skill comes with feelings like "I don't know what I don't know." There has been plenty of research in this space previously (see the massive list of references at the end), and it can be difficult to know which to read (and worse, what's outdated!). Our first posts will attempt to document what is already available and give a very high-level summary of the current state of V8 bug hunting and exploitation. Apologies in advance for a lot of "go read this," but we don't want to rephrase some of the great articles already out there. Our future posts will introduce more original material as we talk about vulnerabilities and their patches. We will also cover how to stay up to date with changes to the code base for when our information becomes outdated. We highly recommend skimming the links we provide throughout the series, and coming back to this page whenever you need more details on a certain topic.

We believe that there is still a lot of research to be done in this area. While the V8/Chromium teams have put [countless security measures](https://www.chromium.org/Home/chromium-security/brag-sheet) in place, exploitable bugs are still found on a regular basis. We hope to explain the processes for vulnerability discovery and exploitation, as well as the code base, in a way that will allow more people to begin bug hunting and memory safety research. 

If you are completely new to V8, see this quick except from the README:

> V8 is Google's open source JavaScript engine.
> V8 implements ECMAScript as specified in ECMA-262.
> V8 is written in C++ and is used in Google Chrome, the open source browser from Google.
> V8 can run standalone, or can be embedded into any C++ application.
> V8 Project page: https://v8.dev/docs

## Previous Work

Our goal in this study will be to create as complete a guide as possible to understanding the current state of V8 exploitation, and also its future! However, it would be entirely impossible without the work that has already been accomplished that gave us our own starting point. Here are several references that we used for understanding. There are probably more that we could have listed, but these were the most influential. Many of the topics covered in these articles will be covered in future posts, so there's no need to fully understand everything on this list. However, this will be a great place to come back to if future posts don't provide enough information. There's a list similar to this one on the [zon8 blog](https://zon8.re/posts/v8-chrome-architecture-reading-list-for-vulnerability-researchers/) where you can find even more links.

#### Getting Started

[Introduction to Turbofan by Jeremy Fetiveau](https://doar-e.github.io/blog/2019/01/28/introduction-to-turbofan/)
[Attacking Turbofan by Jeremy Fetiveau](https://doar-e.github.io/presentations/typhooncon2019/AttackingTurboFan_TyphoonCon_2019.pdf)
[v8 exploit by Sakura](http://eternalsakura13.com/2018/05/06/v8/)
[A guided tour through Chrome's javascript compiler by Stephen Röttger](https://docs.google.com/presentation/d/1DJcWByz11jLoQyNhmOvkZSrkgcVhllIlCHmal1tGzaw/edit)
[Intro to Chrome’s V8 from an exploit development angle by Javier Jimenez](https://sensepost.com/blog/2020/intro-to-chromes-v8-from-an-exploit-development-angle/)
[A Deep Dive Into V8 by Diogo Souza](https://blog.appsignal.com/2020/07/01/a-deep-dive-into-v8.html)
[learning-v8 by danbev](https://github.com/danbev/learning-v8)
[v8-starter-guide by push0ebp](https://github.com/push0ebp/v8-starter-guide)
[An Introduction to Speculative Optimization in V8 by Benedikt Meurer](https://ponyfoo.com/articles/an-introduction-to-speculative-optimization-in-v8)
[Node.js V8 internals: an illustrative primer by Vardan Grigoryan](https://codeburst.io/node-js-v8-internals-an-illustrative-primer-83766e983bf6)

#### Development Perspective

[V8 Docs](https://v8.dev/docs/)
[Intermediate Representation](https://docs.google.com/presentation/d/1Z9iIHojKDrXvZ27gRX51UxHD-bKf1QcPzSijntpMJBM/edit#slide=id.g19134d40cb_0_502)
[Turbofan Design](https://docs.google.com/presentation/d/1sOEF4MlF7LeO7uq-uThJSulJlTh--wgLeaVibsbb3tc/edit#slide=id.g5499b9c42_01170)
[An overview of the TurboFan compiler](https://docs.google.com/presentation/d/1H1lLsbclvzyOF3IUR05ZUaZcqDxo7_-8f4yJoxdMooU/edit#slide=id.g18ceb14729_0_92)
[JavaScript Engine Internals by Mathias Bynens](https://www.youtube.com/watch?v=-lt6a9kbc_k)
[JavaScript Engine Fundamentals by Mathias Bynens](https://mathiasbynens.be/notes/shapes-ics)
[Source to Binary Jounrney of V8 javascript engine by Sakura](http://eternalsakura13.com/2018/06/16/nodefest_v8/)
[A tour of V8: object representation by Jay Conrod](https://jayconrod.com/posts/52/a-tour-of-v8-object-representation)
[Understanding V8’s Bytecode by Franziska Hinkelmann](https://medium.com/dailyjs/understanding-v8s-bytecode-317d46c94775)
[NodeJS V8 docs](https://v8docs.nodesource.com/)

#### Exploring Code

[Embedding V8](https://v8.dev/docs/embed)
[C++ Code guide](https://www.chromium.org/developers/cpp-in-chromium-101-codelab)

#### Memory structure

[Visualizing memory management in V8 Engine by Deepu K Sasidharan](https://deepu.tech/memory-management-in-v8/)
[Memory management, JS vs. C++ and understanding V8 by Jasmine Zangi](https://www.fullstackacademy.com/tech-talks/memory-management-js-vs-c-and-understanding-v8)

#### Writing V8 C++ plugins

[Understanding How the Chrome V8 Engine Translates JavaScript into Machine Code by Mayank Tripathi](https://www.freecodecamp.org/news/understanding-the-core-of-nodejs-the-powerful-chrome-v8-engine-79e7eb8af964/)
[Embedding V8 in a C++ application by David Erceg](https://explorerplusplus.com/blog/2019/03/07/embedding-v8-c++-application)
[C++: Embedding the V8 JavaScript Engine by Dustin Oprea](https://dustinoprea.com/2018/09/26/c-embedding-the-v8-javascript-engine/)
[v8pp](https://nicedoc.io/pmed/v8pp)

#### Exploitation 

[Chrome V8 tutorials by vngkv123](https://github.com/vngkv123/aSiagaming/blob/master/Chrome-v8-tutorials/README.md)
[V8 debug writeup by Sakura](http://eternalsakura13.com/2018/08/02/v8_debug/)

#### Specific Bugs

[Circumventing Chrome's hardening of typer bugs by Jeremy Fetiveau](https://doar-e.github.io/blog/2019/05/09/circumventing-chromes-hardening-of-typer-bugs/)
[A Window of Opportunity by Exodus Intelligence](https://blog.exodusintel.com/2019/04/03/a-window-of-opportunity/)
[Patch Gapping Chrome by Exodus Intelligence](https://blog.exodusintel.com/2019/09/09/patch-gapping-chrome/)
[A Eulogy for Patch-Gapping Chrome by István Kurucsai and Vignesh S Rao](https://blog.exodusintel.com/2020/02/24/a-eulogy-for-patch-gapping/)
[The hunt for Chromium issue 1072171 by Javier Jimenez](https://sensepost.com/blog/2020/the-hunt-for-chromium-issue-1072171/)
[Chrome V8 - -CVE-2019-5782 Tianfu Cup Qihoo 360 S0rrymybad by aSiagaming](https://github.com/vngkv123/aSiagaming/blob/master/Chrome-v8-906043/Chrome%20V8%20-%20-CVE-2019-5782%20Tianfu%20Cup%20Qihoo%20360%20S0rrymybad-%20-ENG-.pdf)
[SIMPLE BUGS WITH COMPLEX EXPLOITS by Syed Faraz Abrar](https://www.elttam.com/blog/simple-bugs-with-complex-exploits/)

#### CTF Problem Write-ups

[Exploiting v8: *CTF 2019 oob-v8 by Syed Faraz Abrar](https://syedfarazabrar.com/2019-12-13-starctf-oob-v8-indepth/)
[Exploiting TurboFan Through Bounds Check Elimination by Hanqing Zhao](https://gts3.org/2019/turbofan-BCE-exploit.html)
[Exploiting Chrome V8: Krautflare (35C3 CTF 2018) by Jay Bosamiya](https://www.jaybosamiya.com/blog/2019/01/02/krautflare/)
[Exploiting the Math.expm1 typing bug in V8 by 0x41414141 in ?? ()](https://abiondo.me/2019/01/02/exploiting-math-expm1-v8/)

#### General JIT Compiler Exploitation

[Attacking JavaScript Engines by Saelo](http://www.phrack.org/papers/attacking_javascript_engines.html)
[Exploiting Logic Bugs in JavaScript JIT Engine by Saelo](http://phrack.org/papers/jit_exploitation.html)
[Blackhat Presentation by Saelo](https://saelo.github.io/presentations/blackhat_us_18_attacking_client_side_jit_compilers.pdf)
[JITsploitation Series by Saelo](https://googleprojectzero.blogspot.com/2020/09/jitsploitation-one.html)
