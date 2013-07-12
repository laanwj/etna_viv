libetnaviv
==============

Library for

a) ioctl (kernel interface) wrapping
b) video memory management
c) command buffer and event queue handling
d) context / state delta handling (still incomplete)
e) register description headers
f) converting surfaces and textures from and to Vivante specific tiling formats

Currently used only by the 3D driver in driver/.
A future 2D, SVG or OpenCL driver can share this code.

This library completely wraps the kernel interface, so that clients don't
depend on the specific headers.

