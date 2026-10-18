// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

import 'dart:js_interop';
import 'dart:js_interop_unsafe';

@JS('process')
external _Process get _process;

extension type _Process(JSObject _) implements JSObject {
  external JSObject getBuiltinModule(String id);
  external JSObject get env;
}

extension type _Fs(JSObject _) implements JSObject {
  external JSString readFileSync(String path, String encoding);
  external bool existsSync(String path);
}

_Fs get _fs => _Fs(_process.getBuiltinModule('fs'));

bool fileExists(String path) => _fs.existsSync(path);

String readTextFile(String path) => _fs.readFileSync(path, 'utf8').toDart;

String? environment(String name) => _process.env.getProperty<JSString?>(name.toJS)?.toDart;
