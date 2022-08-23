#define M_PI 3.14159265358979
#define SSIZE 1  // sample size: sampling box has dimensions 2*SSIZE+1 x 2*SSIZE+1

// mostly copied from align/splat
void
eval_gauss(
vec4 cov, vec2 off,
out float weight)
{
  dvec2 eval = clamp(cov.xy, 0.01, 25); // the lower bound has direct impact on sharpness
  dmat2 E = dmat2(cov.z, -cov.w,
  cov.w,  cov.z);
  dvec2 of = E * off;
  weight = clamp(exp(float(-0.5*dot(of/eval, of))), 1e-4, 1);
}

void grad_proc(
vec2 grad,
vec2 off,
out float weight)
{
  mat2 O = mat2(grad.x * grad.x, grad.x * grad.y,
  grad.x * grad.y, grad.y * grad.y);
  vec2 eval;
  vec2 evec0;
  vec2 evec1;
  evd2x2(eval, evec0, evec1, O);

  // test values:
  /*eval = vec2(4, 1);
  evec0 = vec2(1, 0);
  evec1 = vec2(0, -1);*/

  if (eval.y > eval.x)
  {
    eval.xy = eval.yx;
    vec2 tmp = evec0;
    evec0 = evec1;
    evec1 = evec0;
  }

  float l1 = eval.x;
  float l2 = eval.y;

  // configuration parameters
  /*float k_detail = 0.2;
  float k_denoise = 3;
  float k_stretch = 4;
  float k_shrink = 2;*/
  float k_detail = params.k_det;
  float k_denoise = params.k_den;
  float k_stretch = params.k_stret;
  float k_shrink = params.k_shri;
  float D_th = params.d_th;
  float D_tr = params.d_tr;

  float A = clamp(1 + sqrt(l1 / l2), 1, 10);
  float D = clamp(1 - sqrt(l1) / D_tr + D_th, 0, 1);
  // D = 0;
  float k1_ = k_detail * k_stretch * A;
  float k2_ = k_detail / (k_shrink * A);
  float k1 = ((1 - D) * k1_ + D * k_detail * k_denoise);
  k1 *= k1;
  k1 = clamp(k1, params.t, 100);
  float k2 = ((1 - D) * k2_ + D * k_detail * k_denoise);
  k2 *= k2;
  k2 = clamp(k2, params.t, 100);

  vec2 of = vec2(dot(off, evec0), dot(off, evec1));
  weight = clamp(exp(-0.5*dot(of/vec2(k2, k1), of)), 1e-4, 1);
}

vec4
color_vec(    // get a normalized vector with one of r, g, b being 1, all other entries 0 according to bayer mosaic
ivec2 ipos
)
{
  ivec2 boff = ivec2(ipos.x%2, ipos.y%2);// offset inside block
  if (boff == ivec2(1, 0))
  { // green
    return vec4(0, 1, 0, 0);
  }
  else if (boff == ivec2(0, 1))
  { // green
    return vec4(0, 1, 0, 0);
  }
  else if (boff == ivec2(0, 0))
  { // red
    return vec4(1, 0, 0, 0);
  }
  else
  { // blue
    return vec4(0, 0, 1, 0);
  }
}
