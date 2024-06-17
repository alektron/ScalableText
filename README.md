## Easy Scalable Text Rendering on the GPU
Minimal implementation of a text renderer based on the method described by Evan Wallace (@evanw) in his article:
https://medium.com/@evanwallace/easy-scalable-text-rendering-on-the-gpu-c3f4d782c5ac

## Overview
Traditional approaches to text rendering are not very well suited for scalable text.
e.g. when zooming in and out of a 2D editor, texture atlases constantly need to be rebuilt to adjust for the changing resolution requirements. Signed Distance Field (SDF) based approaches mitigate this issue somewhat but still fall short on high zoom levels.
The method described by Evan Wallace is easy to implement works for all zoom levels out of the box.

This minimal C++/OpenGL implementation provides a simple playground to play around with this method.

## Requirements
Not included in this repository but necessary to run it:
- glew.h and the glew binaries (.lib/.dll)
- Arial.ttf font file. Other ttf fonts can be used. The filename must be adjusted in code