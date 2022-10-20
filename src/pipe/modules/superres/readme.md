# super resolution merging

Super resolution merging from Wronski et al. (https://doi.org/10.1145/3306346.3323024)
with a color correction to reduce color artifacts at edges.

## connectors

* `input` the reference input
* `inputX` the candidate input X
* `mvX` the candidate X's motion vectors
* `maskX` the candidate X's mask
* `output` the output

X is from 1 to 10

## parameters
* `cert` : threshold defining the minimum value in the mask that is required to merge the pixel
* `inc` : resolution increase (0 means output resolution = input resolution, 1 double the resolution in each direction)
* `sup` : size of the gaussian. 0 uses the covariance computation that can be configured with additional parameters
* `rot` : rotate the gaussian in case `sup` is not 0
* `ani` : anisotropy of the gaussian in case `sup` is not 0
* `mask` : mask visualization
* `off` : scale the motion vectors
* `image`: the image(s) that is / are used for merging. -1 means merge all, 0 means reference, other values refer to the input with the same number
* `k_det` : parameter for covariance computation
* `k_den` : parameter for covariance computation
* `k_stret` : parameter for covariance computation
* `k_shri` : parameter for covariance computation
* `d_th` : parameter for covariance computation
* `d_tr` : parameter for covariance computation
* `gauss` : visualize gaussians instead of merging to rgb
* `pointc` : do not boost contributions if weight is less than 0. can be used to visualize the point cloud with very small gaussians.
* `dmd` : unused
* `sigmd` : unused
* `s` : unused
* `t` : minimum value for smaller variance (k_2) in gaussian

## additional configuration
* `common.glsl` contains different covariance computations
* `comb_rb.comp` and `cf_rb.comp` have different options for the color correction
* in `config.h` the subpixel offsets for gaussians can be activated
* in `config.h` the reference gradient can be selected instead of individual gradients
