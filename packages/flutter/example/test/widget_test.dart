// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:qrkude_flutter/qrkude_flutter.dart';
import 'package:qrkude_flutter_example/main.dart';

void main() {
  testWidgets('gallery and editor render QR codes', (tester) async {
    tester.view.physicalSize = const Size(1200, 1600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(const QRKudeExampleApp());
    expect(find.byType(QRKudeView), findsWidgets);

    await tester.tap(find.text('Editor'));
    await tester.pumpAndSettle();
    expect(find.byType(QRKudeView), findsOneWidget);
    expect(find.text('no lint warnings'), findsOneWidget);

    await tester.enterText(find.widgetWithText(TextField, 'Label (empty = none)'), 'SCAN ME');
    await tester.pump();
    final size = tester.getSize(find.byType(QRKudeView));
    expect(size.height, greaterThan(size.width));
  });
}
