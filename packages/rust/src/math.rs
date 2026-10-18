// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

//! Float helpers. With `std` the platform math library is used (the same one
//! the C reference links against); without it, `libm`. `sqrt`, `floor`,
//! `fabs` and `fmod` are exact in both, so only trigonometry and `pow` can
//! differ, by at most one ulp, which never shows at the three decimals of
//! the SVG output.

#[cfg(feature = "std")]
mod imp {
    #[inline]
    pub(crate) fn floor(x: f64) -> f64 {
        x.floor()
    }
    #[inline]
    pub(crate) fn sqrt(x: f64) -> f64 {
        x.sqrt()
    }
    #[inline]
    pub(crate) fn cos(x: f64) -> f64 {
        x.cos()
    }
    #[inline]
    pub(crate) fn sin(x: f64) -> f64 {
        x.sin()
    }
    #[inline]
    pub(crate) fn pow(x: f64, y: f64) -> f64 {
        x.powf(y)
    }
    #[inline]
    pub(crate) fn fabs(x: f64) -> f64 {
        x.abs()
    }
    #[inline]
    pub(crate) fn fmod(x: f64, y: f64) -> f64 {
        x % y
    }
    #[inline]
    pub(crate) fn floorf(x: f32) -> f32 {
        x.floor()
    }
    #[inline]
    pub(crate) fn sqrtf(x: f32) -> f32 {
        x.sqrt()
    }
    #[inline]
    pub(crate) fn fabsf(x: f32) -> f32 {
        x.abs()
    }
}

#[cfg(not(feature = "std"))]
mod imp {
    pub(crate) use libm::{cos, fabs, fabsf, floor, floorf, fmod, pow, sin, sqrt, sqrtf};
}

pub(crate) use imp::*;
