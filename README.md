# V8 Internals - A Security Nerd's Prespective

## Abstract
Open source softwares are inherently trusted by users & developers. This is especially true for modern web-browsers like Chromium, which touches user's personal data very frequently. However, browsers are hard to secure, inherently complex machines. Most browsers have their own memory management and system software, almost like an operating system. In this study we present internals of V8 engine, which is the Javascript engine powering Chromium web-browser. We analyze the major components in depth and then dive into taint-tracking implementation / previous vulnerabilties in order to expolore the current landscape in a systematic way. Our intent is to provide a one stop location to understand the core V8 internals, while looking for opporunities for improvement from a security prespective. 

## Introduction
The twenty first centry can be thought of as a centry of open source software. From industrical systems all the way to your microware might be running a version of an opersource software.While we use a lost fo open source software indirectly, web browsers is a unique kind of software. From an application prespecitive, it allows us to interact with the world and do anything from sending an email, all the way to stream videos and games via cloud. On the software end, it has characteristics of an application software and system software all at once.
We trust our Web browser all of our personal data. But where does our trust come from? It might come from the MNC backing up the software, or it might come from the fact that it's open source. In this study, we look at the trust and power we gain from the software being open souce.
<sup>[[1]](#1)</sup>
<sup>[[2]](#2)</sup>

## Background

## Internals

// a sub-heading (###) for each major topic in our posts

## References
[1]: M. Curphey and D. A. Wheeler, “Improving Trust and Security in Open Source Projects,” p. 27.
[2]: