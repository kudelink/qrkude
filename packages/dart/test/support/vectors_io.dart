// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

import 'dart:io';

bool fileExists(String path) => File(path).existsSync();

String readTextFile(String path) => File(path).readAsStringSync();

String? environment(String name) => Platform.environment[name];
