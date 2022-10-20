# lucas kanade optical flow

Optical flow from Lucas and Kanade.
This implementation uses the equations from Duvenhage et al. (https://doi.org/10.1145/1811158.1811172)

## connectors

* `alignsrc`  a feature map of the to-be-aligned image
* `aligndst`  a feature map to be aligned to (these will stay static)
* `input`  the input image pixels which will be warped
* `output` the warped output image
* `mv_in`   the initial 2d motion vectors used as a starting point (is set to 0 if nothing is connected)
* `mv`     the 2d motion vectors (as input to other modules)

## parameters
* `rounds` : the number of iterations

## additional configuration
* in `lk.comp` the radius can be changed that is used to average the next step
