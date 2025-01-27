// this uses the coefficient cube optimiser from the paper:
//
// Wenzel Jakob and Johannes Hanika. A low-dimensional function space for
// efficient spectral upsampling. Computer Graphics Forum (Proceedings of
// Eurographics), 38(2), March 2019. 
//

// run like
// make && ./xy2sig 512 lut.pfm XYZ && eu lut.pfm -w 1400 -h 1400

// for every pixel in the xy chromaticity graph, do:
// - match c0 c1 c2 or equivalently c0 y lambda
// - explicitly instantiate resulting spectrum and numerically gauss blur it.
// - compute xy position and store velocity field from source to the gauss blurred instance
// TODO:
// as a second step, using this 2D (c0 y lambda vx vy) map
// - create another 2D (s, lambda) map as say 1024x1024 s=0..1 lambda=360..830
// for phi in circle around white point:
// - create spectrum for xy
// - walk velocity field both directions towards white (s=0) and spectral (s=1)
// - store result in largeish array
// - normalise range to resolution of 2D map, resample into row of texture
// - row will have: s=-1..0..1 and is filled in two parts (c0 > 0 and c0 < 0)

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "details/lu.h"
#include "details/matrices.h"
#include "mom.h"
#include "clip.h"
#include "../o-pfm/half.h"

#define BAD_CMF
#ifdef BAD_CMF
// okay let's also hack the cie functions to our taste (or the gpu approximations we'll do)
#define CIE_SAMPLES 30
#define CIE_FINE_SAMPLES 30
#define CIE_LAMBDA_MIN 400.0
#define CIE_LAMBDA_MAX 700.0
#else
/// Discretization of quadrature scheme
#define CIE_SAMPLES 95
#define CIE_LAMBDA_MIN 360.0
#define CIE_LAMBDA_MAX 830.0
#define CIE_FINE_SAMPLES ((CIE_SAMPLES - 1) * 3 + 1)
#endif
#define RGB2SPEC_EPSILON 1e-4
#define MOM_EPS 1e-3

#include "details/cie1931.h"

/// Precomputed tables for fast spectral -> RGB conversion
double lambda_tbl[CIE_FINE_SAMPLES],
       phase_tbl[CIE_FINE_SAMPLES],
       rgb_tbl[3][CIE_FINE_SAMPLES],
       rgb_to_xyz[3][3],
       xyz_to_rgb[3][3],
       xyz_whitepoint[3];

/// Currently supported gamuts
typedef enum Gamut {
    SRGB,
    ProPhotoRGB,
    ACES2065_1,
    ACES_AP1,
    REC2020,
    ERGB,
    XYZ,
} Gamut;

double sigmoid(double x) {
    return 0.5 * x / sqrt(1.0 + x * x) + 0.5;
}

#if 0
// gauss blur a spectrum explicitly:
static inline void gauss_blur(
    const double  sigma_nm,         // in nanometers
    const double *spectrum,
    double       *spectrum_blur,
    const int     cnt,
    const int     u_shape)          // for u-shapes, 1-blur(1-spec)
{
  const double sigma = sigma_nm * cnt / (double)CIE_FINE_SAMPLES; // in bin widths
  const int r = 3*sigma;
  double max = 0.0;
  for(int i=0;i<cnt;i++) spectrum_blur[i] = 0.0;
  for(int i=0;i<cnt;i++)
  {
    double w = 0.0;
    for(int j=-r;j<=r;j++)
    {
      if(i+j < 0 || i+j >= cnt) continue;
      double wg = exp(-j*j / (2.0*sigma*sigma));
      if(u_shape) spectrum_blur[i] += (1.0 - spectrum[i+j]) * wg;
      else        spectrum_blur[i] += spectrum[i+j] * wg;
      w += wg;
    }
    spectrum_blur[i] /= w;
    max = fmax(max, spectrum_blur[i]);
  } // end gauss blur the spectrum loop
  if(u_shape) for(int i=0;i<cnt;i++) spectrum_blur[i] = 1.0 - spectrum_blur[i] / max;
  else        for(int i=0;i<cnt;i++) spectrum_blur[i] /= max;
}
#endif

void lookup2d(float *map, int w, int h, int stride, double *xy, float *res)
{
  double x[] = {xy[0] * w, (1.0-xy[1]) * h};
  x[0] = fmax(0.0, fmin(x[0], w-2));
  x[1] = fmax(0.0, fmin(x[1], h-2));
#if 1 // bilin
  double u[2] = {x[0] - (int)x[0], x[1] - (int)x[1]};
  for(int i=0;i<stride;i++)
    res[i] = (1.0-u[0]) * (1.0-u[1]) * map[stride * (w* (int)x[1]    + (int)x[0]    ) + i]
           + (    u[0]) * (1.0-u[1]) * map[stride * (w* (int)x[1]    + (int)x[0] + 1) + i]
           + (    u[0]) * (    u[1]) * map[stride * (w*((int)x[1]+1) + (int)x[0] + 1) + i]
           + (1.0-u[0]) * (    u[1]) * map[stride * (w*((int)x[1]+1) + (int)x[0]    ) + i];
#else // box
  for(int i=0;i<stride;i++)
    res[i] = map[stride * (w*(int)x[1] + (int)x[0]) +i];
#endif
}

void lookup1d(float *map, int w, int stride, double x, float *res)
{
  x = x * w;
  x = fmax(0.0, fmin(x, w-2));
  double u = x - (int)x;
  for(int i=0;i<stride;i++)
    res[i] = (1.0-u) * map[stride * (int)x + i] + u * map[stride * ((int)x+1) + i];
}

double sqrd(double x) { return x * x; }

void cvt_c0yl_c012(const double *c0yl, double *coeffs)
{
  coeffs[0] = c0yl[0];
  coeffs[1] = c0yl[2] * -2.0 * c0yl[0];
  coeffs[2] = c0yl[1] + c0yl[0] * c0yl[2] * c0yl[2];
}

void cvt_c012_c0yl(const double *coeffs, double *c0yl)
{
  // account for normalising lambda:
  double c0 = CIE_LAMBDA_MIN, c1 = 1.0 / (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
  double A = coeffs[0], B = coeffs[1], C = coeffs[2];

  double A2 = (float)(A*(sqrd(c1)));
  double B2 = (float)(B*c1 - 2*A*c0*(sqrd(c1)));
  double C2 = (float)(C - B*c0*c1 + A*(sqrd(c0*c1)));

  if(fabs(A2) < 1e-12)
  {
    c0yl[0] = c0yl[1] = c0yl[2] = 0.0;
    return;
  }
  // convert to c0 y dom-lambda:
  c0yl[0] = A2;                           // square slope stays
  c0yl[2] = B2 / (-2.0*A2);               // dominant wavelength
  c0yl[1] = C2 - B2*B2 / (4.0 * A2);      // y

#if 0
  double tmp[3];
  tmp[0] = c0yl[0];
  tmp[1] = c0yl[2] * -2.0 * c0yl[0];
  tmp[2] = c0yl[1] + c0yl[0] * c0yl[2] * c0yl[2];
  fprintf(stdout, "%g %g %g -- %g %g %g\n", A2, B2, C2, tmp[0], tmp[1], tmp[2]);
#endif
}

void cvt_c0yl_lwd(const double *c0yl, double *lwd)
{
  // const double A = c0yl[0], B = c0yl[1], C = c0yl[2];
  // const double c0   = A;                        // square slope stays
  // const double ldom = B / (-2.0*A);             // dominant wavelength
  // const double y    = C - B*B / (4.0 * A);      // y

  const double c0   = c0yl[0];
  const double y    = c0yl[1];
  const double ldom = c0yl[2];

  const double y0 = c0 > 0.0 ? 1.0 : -2.0;

  const double w = 2.0 * sqrt((y0 - y)/c0);
  const double d = copysign(sqrt(c0*(y0-y)) * pow(y0*y0+1, -3./2.), c0);

  lwd[0] = ldom;
  lwd[1] = w;
  lwd[2] = d;
}

void quantise_coeffs(double coeffs[3], float out[3])
{
#if 1 // def SIGMOID
  // account for normalising lambda:
  double c0 = CIE_LAMBDA_MIN, c1 = 1.0 / (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
  double A = coeffs[0], B = coeffs[1], C = coeffs[2];

  const double A2 = (A*(sqrd(c1)));
  const double B2 = (B*c1 - 2*A*c0*(sqrd(c1)));
  const double C2 = (C - B*c0*c1 + A*(sqrd(c0*c1)));
  out[0] = (float)A2;
  out[1] = (float)B2;
  out[2] = (float)C2;
#if 0
  {
  const double c0   = A2;                        // square slope stays
  const double ldom = B2 / (-2.0*A2);            // dominant wavelength
  const double y    = C2 - B2*B2 / (4.0 * A2);   // y

  const double y0 = c0 > 0.0 ? 1.0 : -2.0;

  const double w = 2.0 * sqrt((y0 - y)/c0);
  const double d = copysign(sqrt(c0*(y0-y)) * pow(y0*y0+1, -3./2.), c0);

  out[0] = ldom;
  out[1] = w;
  out[2] = d;//fabs(d); /// XXX DEBUG abs to see the output
  }
#endif
#if 0
  // convert to c0 y dom-lambda:
  A = out[0]; B = out[1]; C = out[2];
  out[0] = A;                        // square slope stays
  out[2] = B / (-2.0*A);             // dominant wavelength
  // out[1] = C - A * out[2] * out[2];  // y
  out[1] = C - B*B / (4.0 * A);      // y

  // // these are good bounds:
  // if(out[0] > 0.0 && out[1] >  0.85) fprintf(stdout, "!!!\n");
  // // else fprintf(stdout, "yay, good!\n");
  // if(out[0] < 0.0 && out[1] < -1.85) fprintf(stdout, "!!!??? %g %g %g\n", out[0], out[1], out[2]);
  // // else fprintf(stdout, "yay, good!\n");

  // XXX visualise abs:
  // out[0] = fabsf(out[0]); // goes from 1.0/256.0 (spectral locus) .. 0 (purple ridge through white)
  // out[1] = fabsf(out[1]); // somewhat useful from 0..large purple ridge..spectral locus, but high-low-high for purple tones
  // out[1] = -out[1];
#endif
#if 0
  // convert to shift width slope:
  A = out[0]; B = out[1]; C = out[2];
  // TODO: if 4ac - b^2 > 0:
  int firstcase = 4*A*C - B*B > 0.0;
  if(firstcase)
  {
    out[0] = - sqrt(4*A*C - B*B) / 2.0;   // FIXME something with the signs i don't get
    out[1] = - sqrt(4*A*C - B*B) / (2.0*A);
    out[2] = - B / (2.0 * A);  // dominant wavelength
  } else {
    out[0] = - sqrt(B*B - 4*A*C) / 2.0;
    out[1] = - sqrt(B*B - 4*A*C) / (2.0*A);
    out[2] = - B / (2.0 * A);
  }
  {
  const double slope = out[0], width = out[1], dom_lambda = out[2];
  // TODO: if first case
  double c0, c1, c2;
  c0 = slope/width;
  c1 = -2.0*c0*dom_lambda;
  c2 = c0 * (dom_lambda*dom_lambda - width*width);
  if(4*c0*c2 > c1*c1)
    c2 = slope * width + c0 * dom_lambda*dom_lambda;
  // if(A != 0 || B != 0 || C != 0) fprintf(stderr, "input: %g %g %g\n", slope, width, dom_lambda);
  if(A != 0 || B != 0 || C != 0) fprintf(stderr, "roundtrip: %g %g %g -- %g %g %g \n", A, B, C, c0, c1, c2);
  }
  // slope = +/- sqrt(4 a c - b^2) / 2
  // width = +/- sqrt(4 a c - b^2) / (2a)
  // dlamb = - b / (2a)
  // DEBUG:
  // out[2] = (out[2] - CIE_LAMBDA_MIN) / (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN); /* Scale lambda to 0..1 range */
#endif
#else
  out[0] = coeffs[0];
  out[1] = coeffs[1];
  out[2] = coeffs[2];
#endif
}

void init_coeffs(double coeffs[3])
{
#if 1//def SIGMOID
#ifdef SIG_SWZ
  coeffs[0] = 0.1;
  coeffs[1] = 80.0;
  coeffs[2] = 550.0;
#else
  coeffs[0] = 0.0;
  coeffs[1] = 0.0;
  coeffs[2] = 0.0;
#endif
#else
  coeffs[0] = 0.5;
  coeffs[1] = 0.0;
  coeffs[2] = 0.0;
#endif
}

void clamp_coeffs(double coeffs[3])
{
#ifdef SIG_SWZ
  if(coeffs[2] < 200)  coeffs[2] = 200;
  if(coeffs[2] > 1000) coeffs[2] = 1000;
  if(coeffs[1] < 1e-5) coeffs[1] = 1e-5;
  if(coeffs[1] > 400.0) coeffs[1] = 400.0;
  if(coeffs[0] < -100.0) coeffs[0] = -100.0;
  if(coeffs[0] >  100.0) coeffs[0] =  100.0;
#else
  double max = fmax(fmax(fabs(coeffs[0]), fabs(coeffs[1])), fabs(coeffs[2]));
  if (max > 1000) {
    for (int j = 0; j < 3; ++j)
      coeffs[j] *= 1000 / max;
  }
#endif
}

int check_gamut(double rgb[3])
{
  double xyz[3] = {0.0};
  for(int j=0;j<3;j++)
  for(int i=0;i<3;i++)
    xyz[i] += rgb_to_xyz[i][j] * rgb[j];
  double x = xyz[0] / (xyz[0] + xyz[1] + xyz[2]);
  double y = xyz[1] / (xyz[0] + xyz[1] + xyz[2]);
  return spectrum_outside(x, y);
}


// Journal of Computer Graphics Techniques, Simple Analytic Approximations to
// the CIE XYZ Color Matching Functions Vol. 2, No. 2, 2013 http://jcgt.org
//Inputs:  Wavelength in nanometers
double xFit_1931( double wave )
{
  double t1 = (wave-442.0)*((wave<442.0)?0.0624:0.0374);
  double t2 = (wave-599.8)*((wave<599.8)?0.0264:0.0323);
  double t3 = (wave-501.1)*((wave<501.1)?0.0490:0.0382);
  return 0.362*exp(-0.5*t1*t1) + 1.056*exp(-0.5*t2*t2)- 0.065*exp(-0.5*t3*t3);
}
double yFit_1931( double wave )
{
  double t1 = (wave-568.8)*((wave<568.8)?0.0213:0.0247);
  double t2 = (wave-530.9)*((wave<530.9)?0.0613:0.0322);
  return 0.821*exp(-0.5*t1*t1) + 0.286*exp(-0.5*t2*t2);
}
double zFit_1931( double wave )
{
  double t1 = (wave-437.0)*((wave<437.0)?0.0845:0.0278);
  double t2 = (wave-459.0)*((wave<459.0)?0.0385:0.0725);
  return 1.217*exp(-0.5*t1*t1) + 0.681*exp(-0.5*t2*t2);
}

/**
 * This function precomputes tables used to convert arbitrary spectra
 * to RGB (either sRGB or ProPhoto RGB)
 *
 * A composite quadrature rule integrates the CIE curves, reflectance, and
 * illuminant spectrum over each 5nm segment in the 360..830nm range using
 * Simpson's 3/8 rule (4th-order accurate), which evaluates the integrand at
 * four positions per segment. While the CIE curves and illuminant spectrum are
 * linear over the segment, the reflectance could have arbitrary behavior,
 * hence the extra precations.
 */
void init_tables(Gamut gamut) {
    memset(rgb_tbl, 0, sizeof(rgb_tbl));
    memset(xyz_whitepoint, 0, sizeof(xyz_whitepoint));

    const double *illuminant = 0;

    switch (gamut) {
        case SRGB:
            illuminant = cie_d65;
            memcpy(xyz_to_rgb, xyz_to_srgb, sizeof(double) * 9);
            memcpy(rgb_to_xyz, srgb_to_xyz, sizeof(double) * 9);
            break;

        case ERGB:
            illuminant = cie_e;
            memcpy(xyz_to_rgb, xyz_to_ergb, sizeof(double) * 9);
            memcpy(rgb_to_xyz, ergb_to_xyz, sizeof(double) * 9);
            break;

        case XYZ:
            illuminant = cie_e;
            memcpy(xyz_to_rgb, xyz_to_xyz, sizeof(double) * 9);
            memcpy(rgb_to_xyz, xyz_to_xyz, sizeof(double) * 9);
            break;

        case ProPhotoRGB:
            illuminant = cie_d50;
            memcpy(xyz_to_rgb, xyz_to_prophoto_rgb, sizeof(double) * 9);
            memcpy(rgb_to_xyz, prophoto_rgb_to_xyz, sizeof(double) * 9);
            break;

        case ACES2065_1:
            illuminant = cie_d60;
            memcpy(xyz_to_rgb, xyz_to_aces2065_1, sizeof(double) * 9);
            memcpy(rgb_to_xyz, aces2065_1_to_xyz, sizeof(double) * 9);
            break;

        case ACES_AP1:
            illuminant = cie_d60;
            memcpy(xyz_to_rgb, xyz_to_aces_ap1, sizeof(double) * 9);
            memcpy(rgb_to_xyz, aces_ap1_to_xyz, sizeof(double) * 9);
            break;

        case REC2020:
            illuminant = cie_d65;
            memcpy(xyz_to_rgb, xyz_to_rec2020, sizeof(double) * 9);
            memcpy(rgb_to_xyz, rec2020_to_xyz, sizeof(double) * 9);
            break;
    }

    double norm = 0.0, n2[3] = {0.0};
    for (int i = 0; i < CIE_FINE_SAMPLES; ++i) {

#ifndef BAD_CMF
      double h = (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN) / (CIE_FINE_SAMPLES - 1.0);

        double lambda = CIE_LAMBDA_MIN + i * h;
        double xyz[3] = { cie_interp(cie_x, lambda),
                          cie_interp(cie_y, lambda),
                          cie_interp(cie_z, lambda) },
               I = cie_interp(illuminant, lambda);
#else
      double h = (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN) / (double)CIE_FINE_SAMPLES;
        double lambda = CIE_LAMBDA_MIN + (i+0.5) * h;
        // double lambda = CIE_LAMBDA_MIN + i * h;
        double xyz[3] = { cie_interp(cie_x, lambda),
                          cie_interp(cie_y, lambda),
                          cie_interp(cie_z, lambda) };
        // double xyz[3] = {
        //     xFit_1931(lambda),
        //     yFit_1931(lambda),
        //     zFit_1931(lambda), },
        const double Iw = cie_interp(illuminant, lambda);
             // I = blackbody_radiation(lambda, 6504.0);
  const double cw[3] = {
     -9.16167e-06, 0.00870653, -2.35259 // d65 / 3
     // 0.00014479, -0.189595, 62.5251 // d65 / 1.1
    // 0, 0, 10000 // illum E
      //  -14.1899, 13622.4, -3.26377e+06
    // -8.2609e-05, 0.0823704, -25.0921
    // 0.00395809, -4.02143, 1021.5
    // -9.12318e-05, 0.0924729, -27.9558
    // 0.0691431, -74.2713, 19943.2 // says rec2020
    //  0.0871685, -94.3229, 25511.3 // says xyz
  };
  const double Is = 1.0/106.8 * sigmoid(cw[2] + lambda*(cw[1] + cw[0]*lambda));
  // fprintf(stderr, "%g %g %g\n", Is, Iw, lambda);
  const double I = Iw;
#endif
        norm += I;

#ifndef BAD_CMF
        double weight = 3.0 / 8.0 * h;
        if (i == 0 || i == CIE_FINE_SAMPLES - 1)
            ;
        else if ((i - 1) % 3 == 2)
            weight *= 2.f;
        else
            weight *= 3.f;
#else
        double weight = h;
#endif

#if 0 // output table for shader code
        double out[3] = {0.0};
        for (int k = 0; k < 3; ++k)
            for (int j = 0; j < 3; ++j)
                out[k] += xyz_to_rgb[k][j] * xyz[j];
        fprintf(stderr, "vec3(%g, %g, %g), // %g nm\n", out[0], out[1], out[2], lambda);
#endif
        lambda_tbl[i] = lambda;
        phase_tbl[i] = mom_warp_lambda(lambda);
        for (int k = 0; k < 3; ++k)
            for (int j = 0; j < 3; ++j)
                rgb_tbl[k][i] += xyz_to_rgb[k][j] * xyz[j] * I * weight;

        for (int k = 0; k < 3; ++k)
            xyz_whitepoint[k] += xyz[k] * I * weight;
        for (int k = 0; k < 3; ++k)
            n2[k] += xyz[k] * weight;
    }
}

void eval_residual(const double *coeff, const double *rgb, double *residual)
{
    double out[3] = { 0.0, 0.0, 0.0 };

    for (int i = 0; i < CIE_FINE_SAMPLES; ++i)
    {
      // the optimiser doesn't like nanometers.
      // we'll do the normalised lambda thing and later convert when we write out.
#ifndef SIG_SWZ
#ifdef BAD_CMF
      double lambda = (i+.5)/(double)CIE_FINE_SAMPLES;//(lambda_tbl[i] - CIE_LAMBDA_MIN) / (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN); /* Scale lambda to 0..1 range */
#else
      double lambda = i/(double)CIE_FINE_SAMPLES;//(lambda_tbl[i] - CIE_LAMBDA_MIN) / (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN); /* Scale lambda to 0..1 range */
#endif
      double cf[3] = {coeff[0], coeff[1], coeff[2]};
#else
      double lambda = lambda_tbl[i];
      // float x = rgb2spec_fma(rgb2spec_fma(coeff[0], lambda, coeff[1]), lambda, coeff[2]),
      //       y = 1.0f / sqrtf(rgb2spec_fma(x, x, 1.0f));
      // float s = rgb2spec_fma(.5f * x, y, .5f);
      double slope      = coeff[0];
      double width      = coeff[1];
      double dom_lambda = coeff[2];
#if 0 // from alisa's jupyter notebook:
      double s = slope;      // coeff[0]
      double w = width;      // coeff[1]
      double z = dom_lambda; // coeff[2]

      const double t = (fabs(s) * w + sqrt(s*s*w*w + 1.0/9.0) ) / (2.0*fabs(s)*w);
      const double sqrt_t = sqrt(t);
      const double c0 = s * sqrt_t*sqrt_t*sqrt_t / w;
      const double c1 = -2.0 * c0 * z;
      const double c2 = c0 * z*z + s*w*sqrt_t*(5.0*t - 6.0);
#endif
#if 1 // my simpler version (but forgot what the values mean)
      const double c0 = slope/width;
      const double c1 = -2.0*c0*dom_lambda;
      const double c2 = slope * width + c0 * dom_lambda*dom_lambda;
      // this has the advantage that mathematica can invert it:
      // slope = +/- sqrt(4 a c - b^2) / 2
      // width = +/- sqrt(4 a c - b^2) / (2a)
      // dlamb = - b / (2a)
#endif
      double cf[3] = {c0, c1, c2};
#endif

      { // scope
        /* Polynomial */
        double x = 0.0;
        for (int i = 0; i < 3; ++i)
          x = x * lambda + cf[i];

        /* Sigmoid */
        double s = sigmoid(x);

        /* Integrate against precomputed curves */
        for (int j = 0; j < 3; ++j)
          out[j] += rgb_tbl[j][i] * s;
      }
    }
    // cie_lab(out);
    memcpy(residual, rgb, sizeof(double) * 3);
    // cie_lab(residual);

    for (int j = 0; j < 3; ++j)
        residual[j] -= out[j];
}

void eval_jacobian(const double *coeffs, const double *rgb, double **jac) {
    double r0[3], r1[3], tmp[3];

    for (int i = 0; i < 3; ++i) {
        memcpy(tmp, coeffs, sizeof(double) * 3);
        tmp[i] -= RGB2SPEC_EPSILON;
        eval_residual(tmp, rgb, r0);

        memcpy(tmp, coeffs, sizeof(double) * 3);
        tmp[i] += RGB2SPEC_EPSILON;
        eval_residual(tmp, rgb, r1);

        for(int j=0;j<3;j++) assert(r1[j] == r1[j]);
        for(int j=0;j<3;j++) assert(r0[j] == r0[j]);

        for (int j = 0; j < 3; ++j)
            jac[j][i] = (r1[j] - r0[j]) * 1.0 / (2 * RGB2SPEC_EPSILON);
    }
}

double gauss_newton(const double rgb[3], double coeffs[3])
{
  int it = 40;//15;
    double r = 0;
    for (int i = 0; i < it; ++i) {
        double J0[3], J1[3], J2[3], *J[3] = { J0, J1, J2 };

        double residual[3];

        clamp_coeffs(coeffs);
        eval_residual(coeffs, rgb, residual);
        eval_jacobian(coeffs, rgb, J);

#if 0
        // fix boundary issues when the coefficients do not change any more (some colours may be outside the representable range)
        const double eps = 1e-6;
        for(int j=0;j<3;j++)
        {
          if(fabs(J0[j]) < eps) J0[j] = ((drand48() > 0.5) ? 1.0 : -1.0)*eps*(0.5 + drand48());
          if(fabs(J1[j]) < eps) J1[j] = ((drand48() > 0.5) ? 1.0 : -1.0)*eps*(0.5 + drand48());
          if(fabs(J2[j]) < eps) J2[j] = ((drand48() > 0.5) ? 1.0 : -1.0)*eps*(0.5 + drand48());
        }
#endif

        int P[4];
        int rv = LUPDecompose(J, 3, 1e-15, P);
        if (rv != 1) {
          fprintf(stdout, "RGB %g %g %g -> %g %g %g\n", rgb[0], rgb[1], rgb[2], coeffs[0], coeffs[1], coeffs[2]);
          fprintf(stdout, "J0 %g %g %g\n", J0[0], J0[1], J0[2]);
          fprintf(stdout, "J1 %g %g %g\n", J1[0], J1[1], J1[2]);
          fprintf(stdout, "J2 %g %g %g\n", J2[0], J2[1], J2[2]);
          return 666.0;
        }

        double x[3];
        LUPSolve(J, P, residual, 3, x);

        r = 0.0;
        for (int j = 0; j < 3; ++j) {
            coeffs[j] -= x[j];
            r += residual[j] * residual[j];
        }

        if (r < 1e-6)
            break;
    }
    return sqrt(r);
}

static Gamut parse_gamut(const char *str)
{
  if(!strcasecmp(str, "sRGB"))
    return SRGB;
  if(!strcasecmp(str, "eRGB"))
    return ERGB;
  if(!strcasecmp(str, "XYZ"))
    return XYZ;
  if(!strcasecmp(str, "ProPhotoRGB"))
    return ProPhotoRGB;
  if(!strcasecmp(str, "ACES2065_1"))
    return ACES2065_1;
  if(!strcasecmp(str, "ACES_AP1"))
    return ACES_AP1;
  if(!strcasecmp(str, "REC2020"))
    return REC2020;
  return SRGB;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Syntax: xy2sig <resolution> <output> [<gamut>]\n"
               "where <gamut> is one of sRGB,eRGB,XYZ,ProPhotoRGB,ACES2065_1,ACES_AP1,REC2020\n");
        exit(-1);
    }
    Gamut gamut = SRGB;
    if(argc > 3) gamut = parse_gamut(argv[3]);
    init_tables(gamut);

    const int res = atoi(argv[1]); // resolution of 2d lut

    printf("Optimizing ");
    { // determine white coefficients so we can replace D65 by something faster to evaluate than the array data:
      double coeffs[3] = {0, 0, 1000};//{0.0691431, -74.2713, 19943.2};
      init_coeffs(coeffs);
      // double rgb[3] = {0.95047, 1.0, 1.08883}; // xyz d65
      double rgb[3] = {1, 1, 1}; // illum E
      double b = rgb[0] + rgb[1] + rgb[2];
      rgb[0] /= b;
      rgb[1] /= b;
      rgb[2] /= b;
      double resid = gauss_newton(rgb, coeffs);
      float out[3];
      quantise_coeffs(coeffs, out);
      // fprintf(stderr, "white: %g, %g, %g resid %g\n", out[0], out[1], out[2], resid);
    }

    // read grey map from macadam:
    int max_w, max_h;
    float *max_b = 0;
    {
      // convert macad.pfm -fx 'r' -colorspace Gray -blur 15x15 brightness.pfm
      FILE *f = fopen("brightness.pfm", "rb");
      if(!f)
      {
        fprintf(stderr, "could not read macadam.pfm!!\n");
        exit(2);
      }
      fscanf(f, "Pf\n%d %d\n%*[^\n]", &max_w, &max_h);
      max_b = calloc(sizeof(float), max_w*max_h);
      fgetc(f); // \n
      fread(max_b, sizeof(float), max_w*max_h, f);
      fclose(f);
    }

    int lsres = res/4; // allocate enough for mip maps too
    float *lsbuf = calloc(sizeof(float), 2* 5*lsres*lsres);

    size_t bufsize = 5*res*res;
    float *out = calloc(sizeof(float), bufsize);
#if defined(_OPENMP)
#pragma omp parallel for schedule(dynamic) shared(stdout,out,max_b,max_w,max_h)
#endif
  for (int j = 0; j < res; ++j)
  {
    // const double y = (res - 1 - (j+0.5)) / (double)res;
    const double y = (res - 1 - (j)) / (double)res;
    printf(".");
    fflush(stdout);
    for (int i = 0; i < res; ++i)
    {
      // const double x = (i+0.5) / (double)res;
      const double x = (i) / (double)res;
      double rgb[3];
      // range of fourier moments is [0,1]x[-1/pi,+1/pi]^2
      double coeffs[3];
      init_coeffs(coeffs);
      // normalise to max(rgb)=1
      rgb[0] = x;
      rgb[1] = y;
      rgb[2] = 1.0-x-y;
      if(check_gamut(rgb)) continue;

      int ii = (int)fmin(max_w - 1, fmax(0, i * (max_w / (double)res)));
      int jj = max_h - 1 - (int)fmin(max_h - 1, fmax(0, j * (max_h / (double)res)));
      double m = fmax(0.001, 0.5*max_b[ii + max_w * jj]);
      double rgbm[3] = {rgb[0] * m, rgb[1] * m, rgb[2] * m};
      double resid = gauss_newton(rgbm, coeffs);
      (void)resid;

      double c0yl[3], lwd[3];
      cvt_c012_c0yl(coeffs, c0yl);
      // cvt_c0yl_lwd(c0yl, lwd);
      // fprintf(stderr, "%g %g %g %g %g\n", lwd[0], lwd[1], lwd[2], x, y);
      double velx = 0.0, vely = 0.0;
#if 0
      // TODO: now that we have a good spectrum:
      // explicitly instantiate it
      // explicitly gauss blur it
      // convert back to xy
      // store pointer to this other pixel
      const int cnt = CIE_FINE_SAMPLES;
      double spectrum[cnt];
      double spectrum_blur[cnt];
      for (int l = 0; l < cnt; l++)
      {
        double lambda = l/(cnt-1.0);
        // double lambda = (lambda_tbl[l] - CIE_LAMBDA_MIN) / (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN); /* Scale lambda to 0..1 range */
        double x = 0.0;
        for (int i = 0; i < 3; ++i)
          x = x * lambda + coeffs[i];
        spectrum[l] = sigmoid(x);
      }
      const double sigma = 9.0; // FIXME: < 9 results in banding, > 12 results in second attractor
      gauss_blur(sigma, spectrum, spectrum_blur, cnt, 0);//coeffs[0] > 0.0);
      double col[3] = {0.0};
#if 1 // cnt = CIE_FINE_SAMPLES
      for (int l = 0; l < cnt; l++)
        for (int j = 0; j < 3; ++j)
          col[j] += rgb_tbl[j][l] * spectrum_blur[l];
#else // otherwise
      for (int l = 0; l < cnt; l++)
      {
        double lambda = CIE_LAMBDA_MIN + l/(cnt-1.0) * (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
        double xyz[3] = { cie_interp(cie_x, lambda),
                          cie_interp(cie_y, lambda),
                          cie_interp(cie_z, lambda) };
        col[0] += xyz[0] * spectrum_blur[l];
        col[1] += xyz[1] * spectrum_blur[l];
        col[2] += xyz[2] * spectrum_blur[l];
      }
#endif
      // col is in XYZ, we want the chromaticity coordinates:
      double b = col[0]+col[1]+col[2];
      // velocity vector:
      velx = col[0] / b - x;
      vely = col[1] / b - y;
      double speed = sqrt(velx*velx + vely*vely);
      velx /= speed; // normalise
      vely /= speed;
#endif

      int idx = j*res + i;
      out[5*idx + 0] = coeffs[0];
      out[5*idx + 1] = coeffs[1];
      out[5*idx + 2] = coeffs[2];
      out[5*idx + 3] = c0yl[2];//velx;//m;
      float xy[2] = {x, y}, white[2] = {1.0f/3.0f, 1.0f/3.0f}; // illum E //{.3127266, .32902313}; // D65
      float sat = spectrum_saturation(xy, white);
      out[5*idx + 4] = sat;

      // bin into lambda/saturation buffer
      float satc = lsres * sat;
      float lamc = (c0yl[2] - CIE_LAMBDA_MIN)/(CIE_LAMBDA_MAX-CIE_LAMBDA_MIN) * lsres / 2;
      int lami = fmaxf(0, fminf(lsres/2-1, lamc));
      int sati = satc;
      if(c0yl[0] > 0) lami += lsres/2;
      lami = fmaxf(0, fminf(lsres-1, lami));
      sati = fmaxf(0, fminf(lsres-1, sati));
      float olamc = lsbuf[5*(lami*lsres + sati)+3];
      float osatc = lsbuf[5*(lami*lsres + sati)+4];
      float odist = 
        (olamc - lami - 0.5f)*(olamc - lami - 0.5f)+
        (osatc - sati - 0.5f)*(osatc - sati - 0.5f);
      float  dist = 
        ( lamc - lami - 0.5f)*( lamc - lami - 0.5f)+
        ( satc - sati - 0.5f)*( satc - sati - 0.5f);
      if(dist < odist)
      {
        lsbuf[5*(lami*lsres + sati)+0] = x;
        lsbuf[5*(lami*lsres + sati)+1] = y;
        lsbuf[5*(lami*lsres + sati)+2] = 1.0-x-y;
        lsbuf[5*(lami*lsres + sati)+3] = lamc;
        lsbuf[5*(lami*lsres + sati)+4] = satc;
      }
      out[5*idx + 3] = (lami+0.5f) / (float)lsres;
      out[5*idx + 4] = (sati+0.5f) / (float)lsres;
    }
  }

#if 0
  {
  // TODO: another sanity check: plot all points of some lambda with analytic curves of what we think
  // would be good values for c0 and y!
    // FIXME: for red lambda and n, large s turn around towards white!
    // FIXME: pretty much all turn around for u
    // FIXME: gaps can only be filled on the way back!
  const int lambda_cnt = 32;//512;
  const int sat_cnt = 256;
  // for(int un=0;un<2;un++)
    const int un = 1;
  {
    for(int l=0;l<lambda_cnt;l++)
    {
      for(int s=0;s<sat_cnt;s++)
      {
        double lambda = CIE_LAMBDA_MIN + l/(lambda_cnt-1.0) * (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
        // create spectrum for c0 = +/- 0.01? y=? lambda
        double c0, y;
        if(un) c0 = -pow(s/(sat_cnt-1.0), 3.) * 1./100.; // |c0| in 0..1/256
        // FIXME: want softer progression of c0 to get less extreme blue-white-red ridges
        else   c0 = 0.0000 + 0.0015 * pow(s/(sat_cnt-1.0), 0.8); // c < 0.0015 or 0.002 to go to border
        // else   c0 = 0.0000 + 0.0015 * pow(s/(sat_cnt-1.0), 1./2.); // c < 0.0015 or 0.002 to go to border
        if(un) y = s/(sat_cnt-1.0);
        else y = -0 - 20.0 * s/(sat_cnt-1.0); // want to reach -20 at the border
        double c0yl[3] = {c0, y, lambda};

        double col[3] = {0};
        for (int ll = 0; ll < CIE_FINE_SAMPLES; ll++)
        {
          double l2 = CIE_LAMBDA_MIN + ll/(double)CIE_FINE_SAMPLES * (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
          double x = c0yl[0] * (l2 - c0yl[2])*(l2 - c0yl[2]) + c0yl[1];
          double p = sigmoid(x);
          for(int j=0;j<3;j++)
            col[j] += rgb_tbl[j][ll] * p;
        }
        double xy[] = {
          col[0] / (col[0]+col[1]+col[2]),
          col[1] / (col[0]+col[1]+col[2])};
        fprintf(stderr, "%g %g %d\n", xy[0], xy[1], s);
      }
    }
  }
  }
#endif

#if 0 // circular version
  {
  // now we have a c0 c1 c2 vx vy map.
  // as a second step, using this 2D (c0 y lambda vx vy) map
  // create another 2D (s, lambda) map as say 512x1024:
  const int lambda_cnt = 40;//1024;//512;
  const int sat_cnt = 512;
  float *map = calloc(sizeof(float)*3, lambda_cnt*2*sat_cnt);

  const int stripe_size = 256;//2048;
  float *stripe = calloc(sizeof(float)*3*lambda_cnt, stripe_size);

  int *right = calloc(sizeof(int), lambda_cnt);
  int *left  = calloc(sizeof(int), lambda_cnt);

  for(int l=0;l<lambda_cnt;l++) for(int i=0;i<2*sat_cnt;i++)
  {
    map[3*(2*sat_cnt * l + i) + 0] = 1.0;
    map[3*(2*sat_cnt * l + i) + 1] = 0.0;
    map[3*(2*sat_cnt * l + i) + 2] = 0.0;
  }

  // for(int d=-1;d<=1;d+=2)
    const int d = 1;
  // this isn't in fact a wavelength but an angle
  for(int l=0;l<lambda_cnt;l++)
  {
    // weird offset hand tuned such that the separation n-shape/u-shape goes through y=0 coordinate in the output map:
    double phi = 2.0*M_PI*l/(lambda_cnt-1.0) + 0.48;
    const double radius = 0.10;
    double xy[2] = {1.0/3.0 + radius * cos(phi), 1.0/3.0 + radius * sin(phi)};
    double c0yl[3];
    float px[5];
    lookup2d(out, res, res, 5, xy, px);
    double coeffs[3] = {px[0], px[1], px[2]};
        // cvt_c0yl_c012(c0yl, coeffs);
    cvt_c012_c0yl(coeffs, c0yl);

    const int it_cnt = stripe_size/2;
    for(int it=0;it<it_cnt;it++) 
    {
      double lwd[3];
      cvt_c0yl_lwd(c0yl, lwd);
      fprintf(stderr, "%g %g %g %d %d %g %g\n", xy[0], xy[1], c0yl[2], l, it, lwd[1], lwd[2]);
      {
        double col[3] = {0};
        for (int ll = 0; ll < CIE_FINE_SAMPLES; ll++)
        {
          double l2 = CIE_LAMBDA_MIN + ll/(double)CIE_FINE_SAMPLES * (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
          double x = c0yl[0] * (l2 - c0yl[2])*(l2 - c0yl[2]) + c0yl[1];
          double s = sigmoid(x);
          for(int j=0;j<3;j++)
            col[j] += rgb_tbl[j][ll] * s;
        }
        xy[0] = col[0] / (col[0]+col[1]+col[2]);
        xy[1] = col[1] / (col[0]+col[1]+col[2]);
      }

      // read velocity field at xy and walk a single pixel step:
      lookup2d(out, res, res, 5, xy, px);
      double dir = -1.0;//c0yl[0] > 0.0 ? 1.0 : - 1.0;
      xy[0] += d*dir * px[3] * .5/it_cnt;
      xy[1] += d*dir * px[4] * .5/it_cnt;

      lookup2d(out, res, res, 5, xy, px);
      double new_c[] = {px[0], px[1], px[2]};
      double new_c0yl[3];
      cvt_c012_c0yl(new_c, new_c0yl);

      stripe[3*(stripe_size*l + stripe_size/2 + (d> 0 ? it : -it-1)) + 0] = c0yl[0];
      stripe[3*(stripe_size*l + stripe_size/2 + (d> 0 ? it : -it-1)) + 1] = c0yl[1];
      stripe[3*(stripe_size*l + stripe_size/2 + (d> 0 ? it : -it-1)) + 2] = c0yl[2];

      if(d > 0) right[l] = it; // expand right boundary
      else      left[l]  = it; // expand left boundary (doesn't really work)
      if(c0yl[0] * new_c0yl[0] <= 0.0) break; // n vs u mismatch, streamline borken :(
      // if(fabs(xy[0] - 1.0/3.0) < 1e-4 && fabs(xy[1] - 1.0/3.0) < 1e-4) break;
      if(spectrum_outside(xy[0], xy[1])) break; // outside spectral locus
      // ??
      c0yl[0] = new_c0yl[0];
      c0yl[1] = new_c0yl[1];
      // c0yl[2] = new_c0yl[2]; // keep lambda
    // }

    // for(int i=0;i<it_cnt;i++)
    // {
      // map[3*(2*sat_cnt * l + sat_cnt + (d>0 ? it : -it-1)) + 0] = fabs(c0yl[0]);
      // map[3*(2*sat_cnt * l + sat_cnt + (d>0 ? it : -it-1)) + 1] = fabs(c0yl[1]);
      // map[3*(2*sat_cnt * l + sat_cnt + (d>0 ? it : -it-1)) + 2] = (c0yl[2] - CIE_LAMBDA_MIN)/(CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
    }
  }

  // smooth right boundary:
  for(int i=1;i<lambda_cnt-1;i++)
  {
    int crop = (right[i-1] + right[i+1])/2;
    if(right[i] > crop) right[i] = crop;
  }
  for(int i=1;i<lambda_cnt-1;i++)
  {
    int crop = (left[i-1] + left[i+1])/2;
    if(left[i] > crop) left[i] = crop;
  }
  // resample lines from stripe_size [ss/2..right] to sat_cnt
  for(int l=0;l<lambda_cnt;l++)
  {
    for(int i=0;i<sat_cnt;i++)
    {
      float res[3];
      float f = i/(float)sat_cnt * right[l]/(float)(stripe_size);
      lookup1d(stripe + 3*(stripe_size*l + stripe_size/2), stripe_size, 3, f, res);
      map[3*(2*sat_cnt * l + sat_cnt + i) + 0] = fabsf(res[0]);
      map[3*(2*sat_cnt * l + sat_cnt + i) + 1] = fabsf(res[1]);
      map[3*(2*sat_cnt * l + sat_cnt + i) + 2] = (res[2] - CIE_LAMBDA_MIN)/(CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
    }
    for(int i=0;i<sat_cnt;i++)
    {
      float res[3];
      float f = 0.5 - i/(float)sat_cnt * left[l]/(float)(stripe_size);
      lookup1d(stripe + 3*(stripe_size*l), stripe_size, 3, f, res);
      map[3*(2*sat_cnt * l + sat_cnt - i-1) + 0] = fabsf(res[0]);
      map[3*(2*sat_cnt * l + sat_cnt - i-1) + 1] = fabsf(res[1]);
      map[3*(2*sat_cnt * l + sat_cnt - i-1) + 2] = (res[2] - CIE_LAMBDA_MIN)/(CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
    }
  }
#if 0
  // invalidate samples outside
  for(int l=0;l<lambda_cnt;l++)
  {
    for(int i=right[l];i<sat_cnt;i++)
    {
      map[3*(2*sat_cnt * l + sat_cnt + i) + 0] = 1.0;
      map[3*(2*sat_cnt * l + sat_cnt + i) + 1] = 0.0;
      map[3*(2*sat_cnt * l + sat_cnt + i) + 2] = 0.0;
    }
  }
#endif

  FILE *f = fopen("map.pfm", "wb");
  if(f)
  {
    fprintf(f, "PF\n%d %d\n-1.0\n", 2*sat_cnt, lambda_cnt);
    for(int k=0;k<sat_cnt*2*lambda_cnt;k++)
    {
      float coeffs[3] = {map[3*k+0], map[3*k+1], map[3*k+2]};
      fwrite(coeffs, sizeof(float), 3, f);
    }
    fclose(f);
  }
  } // end scope
#endif


#if 0
  {
  // now we have a c0 c1 c2 vx vy map.
  // as a second step, using this 2D (c0 y lambda vx vy) map
  // create another 2D (s, lambda) map as say 512x1024:
  const int lambda_cnt = 128;//512;
  const int sat_cnt = 64;
  float *map = calloc(sizeof(float)*3, lambda_cnt*2*sat_cnt);

  const int stripe_size = 2048;
  float *stripe = calloc(sizeof(float)*3, stripe_size);

  // for n and u spectra and lambda in [360, 830], do:
  // for(int un=0;un<2;un++)
  const int un = 0;
  {
    for(int l=0;l<lambda_cnt;l++)
    // const int l = lambda_cnt / 2;
    {
      int dir_lower = stripe_size/2, dir_upper = stripe_size/2;
      memset(stripe, 0, sizeof(float)*3*stripe_size);
      // walk velocity field both directions towards white (s=0) and spectral (s=1)
      for(int dir=-1;dir<=1;dir+=2)
      {
        // TODO: could start from a rasterised 2d circle in xy around 1/3 1/3, radius < 0.1 instead!
        double lambda = CIE_LAMBDA_MIN + l/(lambda_cnt-1.0) * (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
        // create spectrum for c0 = +/- 0.01? y=? lambda
        double c0 = un ? -0.0005 : 0.0001; // n case is negative
        double y  = un ? 3 : -2;
        double c0yl[3] = {c0, y, lambda};
        double xy[2] = {0.4, 0.4};
        float px[5];
        // lookup2d(out, res, res, 5, xy, px);
        // double coeffs[3] = {px[0], px[1], px[2]};
        // cvt_c0yl_c012(c0yl, coeffs);
        // cvt_c012_c0yl(coeffs, c0yl);

        const int it_cnt = 150;// TODO: put sane maximum number of steps
        for(int it=0;it<it_cnt;it++) 
        // for(int it=0;it<5;it++) // TODO: put sane maximum number of steps
        {
          // determine xy
           // XXX DEBUG
          if(it==0) // only step xy + vel, don't convert to spectrum in between
          {
          double col[3] = {0};
          for (int ll = 0; ll < CIE_FINE_SAMPLES; ll++)
          {
#if 0
            double l2 = CIE_LAMBDA_MIN + ll/(double)CIE_FINE_SAMPLES * (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
            double c0 = 360.0, c1 = 1.0 / (830.0 - 360.0);
            double A = coeffs[0], B = coeffs[1], C = coeffs[2];

            double A2 = (float)(A*(sqrd(c1)));
            double B2 = (float)(B*c1 - 2*A*c0*(sqrd(c1)));
            double C2 = (float)(C - B*c0*c1 + A*(sqrd(c0*c1)));
            double x = A2*l2*l2 + B2*l2 + C2;
#endif
#if 0
            double l3 = ll/(double)CIE_FINE_SAMPLES;
            double x = 0.0;
            for (int i = 0; i < 3; ++i) x = x * l3 + coeffs[i];
#endif
#if 1
            double l2 = CIE_LAMBDA_MIN + ll/(double)CIE_FINE_SAMPLES * (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);
            double x = c0yl[0] * (l2 - c0yl[2])*(l2 - c0yl[2]) + c0yl[1];
#endif
            double s = sigmoid(x);
            // fprintf(stdout, "%g %g\n", l2, s);
            for(int j=0;j<3;j++)
              col[j] += rgb_tbl[j][ll] * s;
          }
          // XXX FIXME: col seems to stay the same while xy moves along, something is broken here
          // if(it)
          //   fprintf(stderr, "%g %g -- %g %g %d\n", xy[0], xy[1],
          //     col[0] / (col[0]+col[1]+col[2]),
          //     col[1] / (col[0]+col[1]+col[2]),
          //     it);
          xy[0] = col[0] / (col[0]+col[1]+col[2]);
          xy[1] = col[1] / (col[0]+col[1]+col[2]);
          }
          // fprintf(stderr, "%g %g %d\n", xy[0], xy[1], it);

          // read velocity field at xy and walk a single pixel step:
          float px[5];
          lookup2d(out, res, res, 5, xy, px);
          xy[0] += (c0yl[0] > 0.0 ? 1.0 : -1.0) * dir * px[3] * .5/it_cnt;
          xy[1] += (c0yl[0] > 0.0 ? 1.0 : -1.0) * dir * px[4] * .5/it_cnt;
          // double cf[3] = {px[0], px[1], px[2]};
          // fprintf(stderr, "c0yl %g %g %g -- ", c0yl[0], c0yl[1], c0yl[2]);
          // cvt_c012_c0yl(cf, c0yl);
          // fprintf(stderr, "cf %g %g %g  ", cf[0], cf[1], cf[2]);
          // fprintf(stderr, "%g %g %g\n", c0yl[0], c0yl[1], c0yl[2]);

          // store result in largeish array
          const int si = dir < 0 ? dir_lower-- : dir_upper++;
          if(dir_lower < 0 || dir_upper >= stripe_size)
          {
            fprintf(stdout, "array full\n");
            break;
          }
          // fprintf(stdout, "filling %d %g\n", si, c0yl[0]);
          stripe[3*si + 0] = fabs(c0yl[0]);
          stripe[3*si + 1] = fabs(c0yl[1]);
          stripe[3*si + 2] = c0yl[2];
          // stripe[3*si + 2] = CIE_LAMBDA_MIN + l/(double)CIE_FINE_SAMPLES * (CIE_LAMBDA_MAX - CIE_LAMBDA_MIN);

          // terminate if xy close to white
          if(fabs(xy[0] - 1.0/3.0) < 1e-3 &&
             fabs(xy[1] - 1.0/3.0) < 1e-3)
          {
            // fprintf(stdout, "exit white\n");
            break;
          }
          // terminate if xy out of clip range
          if(spectrum_outside(xy[0], xy[1]))
          {
            // fprintf(stdout, "exit border\n");
            break;
          }

          // FIXME: walking only xy + vel is different to round tripping through spectrum!!
          
          // read c0 c1 c2 and convert to c0 y lambda. update c0 and y, keep lambda.
          lookup2d(out, res, res, 5, xy, px);
          // coeffs[0] = px[0]; coeffs[1] = px[1]; coeffs[2] = px[2];
          double new_c[] = {px[0], px[1], px[2]};
          double new_c0yl[3];
          cvt_c012_c0yl(new_c, new_c0yl);
          // TODO: avoid sign change in c0!
          // TODO: n-shaped should not have y < 0 (or so?)
          // TODO: u-shaped should not have y > 1
          // fprintf(stdout, "c0 %g %g y: %g %g\n", c0yl[0], new_c0yl[0], c0yl[1], new_c0yl[1]);
          c0yl[0] = new_c0yl[0];
          c0yl[1] = new_c0yl[1];
          // c0yl[2] = new_c0yl[2]; // keep lambda
          // cvt_c0yl_c012(c0yl, coeffs);
        } // end iterations along direction
      } // end direction forward/back
      // normalise range of stripe (dir_lower, dir_upper) to resolution of 2D map, resample into row of texture
      // row will have: s=-1..0..1 and is filled in two parts (c0 > 0 and c0 < 0)
      // fprintf(stdout, "%d begin end %d %d\n", l, dir_lower, dir_upper);
      // dir_upper = stripe_size/2; // XXX DEBUG
      // dir_lower = 0; // all scanlines for n seem to agree on this quite well
      for(int i=0;i<sat_cnt;i++)
      {
        // convert to index in stripe
        double f = i/(double)sat_cnt;
        float c0yl[3];
        lookup1d(stripe + 3*(dir_lower+1), dir_upper-dir_lower-1, 3, f, c0yl);
        // if(fabsf(c0yl[0]) > 0.0f)
        // fprintf(stdout, "val %d %d [%g]= %g\n", l, i, f * (dir_upper-dir_lower-1), c0yl[0]);
        if(un)
        { // n shapes (spectral colours)
          map[3*(2*sat_cnt * l + sat_cnt + i) + 0] = c0yl[0];
          map[3*(2*sat_cnt * l + sat_cnt + i) + 1] = c0yl[1];
          map[3*(2*sat_cnt * l + sat_cnt + i) + 2] = 0;//c0yl[2];
        }
        else
        { // u shapes (purple line)
          map[3*(2*sat_cnt * l + sat_cnt - i - 1) + 0] = c0yl[0];
          map[3*(2*sat_cnt * l + sat_cnt - i - 1) + 1] = c0yl[1];
          map[3*(2*sat_cnt * l + sat_cnt - i - 1) + 2] = 0;//c0yl[2];
        }
      } // end saturation row for const l

    } // end lambda l
  } // end u-shape n-shape un

  FILE *f = fopen("map.pfm", "wb");
  if(f)
  {
    fprintf(f, "PF\n%d %d\n-1.0\n", 2*sat_cnt, lambda_cnt);
    for(int k=0;k<sat_cnt*2*lambda_cnt;k++)
    {
      float coeffs[3] = {map[3*k+0], map[3*k+1], map[3*k+2]};
      fwrite(coeffs, sizeof(float), 3, f);
    }
    fclose(f);
  }
  } // end scope
#endif

#if 1
  { // scope write lsbuf
#if 0 // superbasic push/pull hole filling. better use gmic's morphological hole filling.
    // gmic lsbuf.pfm  --mul 256 --select_color 0,0,0,0 -inpaint_morpho[0] [1] -rm[1] -o lsbuf2.pfm (only that this doesn't work :( )
  // allocate mipmap memory:
  int num_mips = 0;
  for(int r=lsres;r;r>>=1) num_mips++;
  // push down inited avg to mipmaps:
  int r = lsres;
  float *b0 = lsbuf;
  for(int l=1;l<num_mips;l++)
  {
    int r0 = r;
    float *b1 = b0 + r0 * r0 * 5;
    r >>= 1;
    for(int j=0;j<r;j++) for(int i=0;i<r;i++)
    {
      if(b1[5*(j*r+i)+0] == 0.0f)
      { // average finer res, if inited
        int cnt = 0;
        float avg[5] = {0.0f};
#define PIX(II,JJ) \
        if(b0[5*((2*j+JJ)*r0 + 2*i+II)+0] != 0.0f) { \
          cnt ++;\
          for(int k=0;k<5;k++) avg[k] += b0[5*((2*j+JJ)*r0 + 2*i+II)+k];\
        }
        PIX(0,0);
        PIX(0,1);
        PIX(1,0);
        PIX(1,1);
#undef PIX
        if(cnt) for(int k=0;k<5;k++) b1[5*(j*r+i)+k] = avg[k] / cnt;
      }
    }
    b0 = b1;
  }
  // pull up to uninited hi res
  for(int j=0;j<lsres;j++) for(int i=0;i<lsres;i++)
  {
    if(lsbuf[5*(j*lsres+i)] == 0.0f)
    {
      int ii = i, jj = j, r = lsres;
      float *b1 = lsbuf;
      for(int l=0;l<num_mips;l++)
      {
        b1 += 5*r*r;
        r >>= 1; ii >>= 1; jj >>= 1;
        if(b1[5*(jj*r+ii)] != 0.0f)
        {
          for(int k=0;k<5;k++)
            lsbuf[5*(j*lsres+i)+k] = b1[5*(jj*r+ii)+k];
          break;
        }
      }
    }
  }
#endif
#if 1 // interpolate 0. 0.08 linearly from white to first meaningful values:
#endif
  FILE *f = fopen("lsbuf.pfm", "wb");
  if(f)
  {
    fprintf(f, "PF\n%d %d\n-1.0\n", lsres, lsres);
    for(int j=0;j<lsres;j++) for(int i=0;i<lsres;i++)
      fwrite(lsbuf + j*5*lsres + i*5, sizeof(float), 3, f);
    fclose(f);
  }
#if 0 // DEBUG plot a couple of grid points
  for(int j=0;j<lsres;j+=1)
  for(int i=0;i<lsres;i+=10)
    fprintf(stderr, "%g %g\n",
        lsbuf[3*(j*lsres+i)+0],
        lsbuf[3*(j*lsres+i)+1]);
#endif
  }
#endif
#if 1 // write four channel half lut
  {
  // convert to half
  uint32_t size = 4*sizeof(uint16_t)*res*res;
  uint16_t *b16 = malloc(size);
  for(int k=0;k<res*res;k++)
  {
    double coeffs[3] = {out[5*k+0], out[5*k+1], out[5*k+2]};
    double c0yl[3];
    cvt_c012_c0yl(coeffs, c0yl);
    float q[4] = {c0yl[0], c0yl[1], c0yl[2], out[5*k+4]};
    b16[4*k+0] = float_to_half(1e5f*q[0]);
    b16[4*k+1] = float_to_half(q[1]);
    b16[4*k+2] = float_to_half(q[2]);
    b16[4*k+3] = float_to_half(q[3]);
  }
  typedef struct header_t
  {
    uint32_t magic;
    uint16_t version;
    uint16_t channels;
    uint32_t wd;
    uint32_t ht;
  }
  header_t;
  header_t head = (header_t) {
    .magic    = 1234,
    .version  = 1,
    .channels = 4,
    .wd       = res,
    .ht       = res,
  };
  FILE *f = fopen("sig.lut", "wb");
  if(f)
  {
    fwrite(&head, sizeof(head), 1, f);
    fwrite(b16, size, 1, f);
  }
  fclose(f);
  }
#endif

  FILE *f = fopen(argv[2], "wb");
  if(f)
  {
    fprintf(f, "PF\n%d %d\n-1.0\n", res, res);
    for(int k=0;k<res*res;k++)
    {
      double coeffs[3] = {out[5*k+0], out[5*k+1], out[5*k+2]};
      float q[3];
      quantise_coeffs(coeffs, q);
      // fprintf(stdout, "%g %g %g\n", q[0], q[1], q[2]);
      q[2] = q[0];
      q[0] = out[5*k+3]; // DEBUG lambda tc
      q[1] = out[5*k+4]; // DEBUG saturation tc
#if 1 // coeff data
      fwrite(q, sizeof(float), 3, f);
#else // velocity field
      float vel[3] = {
          out[5*k+3],
          out[5*k+4],
          // 1.0};
          (1.0- out[5*k+3]*out[5*k+3]- out[5*k+4]*out[5*k+4])};
      // vel[0] = 0.5 + 0.5*vel[0];
      // vel[1] = 0.5 + 0.5*vel[1];
      // vel[2] = 0.5 + 0.5*vel[2];
      fwrite(vel, sizeof(float), 3, f);

      // fwrite(out+5*k+0, sizeof(float), 1, f);
      // fwrite(out+5*k+3, sizeof(float), 2, f);
      // fwrite(&one, sizeof(float), 1, f);
#endif
    }
    fclose(f);
  }
  free(out);
  printf("\n");
}
