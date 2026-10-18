// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// Loads the conformance vectors from `spec/vectors/` (VM: dart:io,
/// JavaScript: Node's `fs` module via `process.getBuiltinModule`).
library;

import 'dart:convert';

import 'vectors_node.dart' if (dart.library.io) 'vectors_io.dart';

/// Path of the repository's `spec/vectors` directory, relative to the
/// package root (the working directory of `dart test`). Override with the
/// `QRKUDE_VECTORS` environment variable.
String vectorsDir() => environment('QRKUDE_VECTORS') ?? '../../spec/vectors';

/// The vectors of `<name>.json`, or null when the file is not available
/// (e.g. the package was extracted outside the QRKude repository).
List<Map<String, Object?>>? loadVectors(String name) {
  final path = '${vectorsDir()}/$name.json';
  if (!fileExists(path)) return null;
  return (jsonDecode(readTextFile(path)) as List<Object?>).cast<Map<String, Object?>>();
}
