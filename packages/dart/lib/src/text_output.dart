// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

/// Text output (SPEC 14): the module grid written as text, for terminals.
/// The output is identical across all QRKude ports.
library;

import 'encoder.dart';

/// Characters used by [toText] (SPEC 14.3, 14.4).
///
/// Not named `TextStyle`, which would clash with Flutter's `TextStyle` in
/// apps that import both libraries.
enum TextOutputStyle {
  /// One character for two module rows: `' '`, `'▀'`, `'▄'` or `'█'`
  /// (the default, the most compact).
  halfBlocks,

  /// `'██'` for a dark module, two spaces for a light one; one line per
  /// module row.
  fullBlocks,

  /// `'##'` for a dark module, two spaces for a light one; one line per
  /// module row. For terminals without UTF-8, or that show the block
  /// characters two columns wide.
  hash,
}

/// Color codes written by [toText] (SPEC 14.5).
enum TextColors {
  /// Plain characters, no escape codes (the default).
  none,

  /// Each line in black on bright white: `ESC[30;107m` before the cells and
  /// `ESC[0m` before the line break. Readable in dark terminal themes.
  ansi,
}

/// Options of [toText] and [textSize] (SPEC 14.1).
class TextOptions {
  /// Creates text options; the defaults give half blocks with a margin of 2,
  /// no inversion and no colors.
  const TextOptions({
    this.style = TextOutputStyle.halfBlocks,
    this.margin = 2,
    this.invert = false,
    this.colors = TextColors.none,
  });

  /// The characters used.
  final TextOutputStyle style;

  /// Light modules on each side (the quiet zone), clamped to 0..16 when the
  /// text is written. 2 lets a version 6 code fit an 80x24 terminal.
  final int margin;

  /// Swaps dark and light modules, margin included.
  final bool invert;

  /// Whether each line is wrapped in ANSI color codes.
  final TextColors colors;

  @override
  bool operator ==(Object other) =>
      other is TextOptions &&
      other.style == style &&
      other.margin == margin &&
      other.invert == invert &&
      other.colors == colors;

  @override
  int get hashCode => Object.hash(style, margin, invert, colors);

  @override
  String toString() => 'TextOptions(style: ${style.name}, margin: $margin, invert: $invert, colors: ${colors.name})';
}

const _marginMax = 16;
const _halfCells = [' ', '\u2580', '\u2584', '\u2588']; // ' ', '▀', '▄', '█'
const _fullCells = ['  ', '\u2588\u2588'];
const _hashCells = ['  ', '##'];

/// Writes [code] as text (SPEC 14), every line ending with `"\n"`.
///
/// Only square modules are drawn: the style object does not apply. Print it
/// with `stdout.write` (not `print`, which adds a blank line).
String toText(QRCode code, [TextOptions options = const TextOptions()]) {
  final n = code.size, m = options.margin.clamp(0, _marginMax), invert = options.invert;
  final half = options.style == TextOutputStyle.halfBlocks;
  final cells = switch (options.style) {
    TextOutputStyle.halfBlocks => _halfCells,
    TextOutputStyle.fullBlocks => _fullCells,
    TextOutputStyle.hash => _hashCells,
  };
  final ansi = options.colors == TextColors.ansi;
  final out = StringBuffer();
  for (var y = -m; y < n + m; y += half ? 2 : 1) {
    if (ansi) out.write('\x1B[30;107m');
    for (var x = -m; x < n + m; x++) {
      var index = code.isDark(x, y) != invert ? 1 : 0;
      if (half && code.isDark(x, y + 1) != invert) index |= 2;
      out.write(cells[index]);
    }
    if (ansi) out.write('\x1B[0m');
    out.write('\n');
  }
  return out.toString();
}

/// The size of [toText]'s output: `columns` per line (ANSI codes take
/// none) and the number of `lines`.
({int columns, int lines}) textSize(QRCode code, [TextOptions options = const TextOptions()]) {
  final side = code.size + 2 * options.margin.clamp(0, _marginMax);
  return options.style == TextOutputStyle.halfBlocks
      ? (columns: side, lines: (side + 1) ~/ 2)
      : (columns: 2 * side, lines: side);
}
