# image alignment

this module implements a hierarchical block-based image alignment algorithm presented in 

[Samuel W. Hasinoff, Dillon Sharlet, Ryan Geiss, Andrew Adams, Jonathan T. Barron, Florian Kainz, 
Jiawen Chen, and Marc Levoy, Burst photography for high dynamic range and low-light imaging on mobile 
cameras](https://people.csail.mit.edu/hasinoff/pubs/HasinoffEtAl16-hdrplus.pdf).

## connectors

* `ref`    the reference image that the input is aligned to
* `input`  the input image pixels which will be warped
* `output` the 2d motion vectors
* `mask`   the error mask (output)
