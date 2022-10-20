# robustness mask using a guide image and motion vectors

robustness mask from wronski et al. (https://doi.org/10.1145/3306346.3323024)
## connectors

* `input`  the candidate image that
* `output` robustness mask as input to other modules
* `mv`     the 2d motion vectors that warp the input to the reference
* `ref`    the reference image

## parameters
dmd:float:1:0
sigmd:float:1:0.03
s:float:1:10
t:float:1:1.5
smot:float:1:2.3
mth:float:1:0.2
* `dmd` : color difference from noise model
* `sigmd` : variance from noise model
* `s` : scale up the metric
* `t` : subtracted from the metric
* `smot` : `s` in case the motion variation is larger than `mth`
* `mth` : threshold for motion variation

## additional configuration
* in `motion.comp` and `min5.comp` the radius can be changed
