// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

use core::fmt;

/// Errors reported by QRKude.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum Error {
    /// An option is out of range (version bounds, mask, raster size...).
    InvalidArgument,
    /// The payload does not fit in any allowed version (`"data too long"`).
    DataTooLong,
    /// A pixel buffer is smaller than the requested image.
    BufferTooSmall,
    /// An output buffer could not be allocated (e.g. absurd raster sizes).
    OutOfMemory,
    /// The style JSON document is malformed or has a value of the wrong type.
    Parse,
}

impl Error {
    /// The message used by every QRKude port (`"data too long"`, ...).
    pub fn as_str(self) -> &'static str {
        match self {
            Error::InvalidArgument => "invalid argument",
            Error::DataTooLong => "data too long",
            Error::BufferTooSmall => "buffer too small",
            Error::OutOfMemory => "out of memory",
            Error::Parse => "parse error",
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

#[cfg(feature = "std")]
impl std::error::Error for Error {}
