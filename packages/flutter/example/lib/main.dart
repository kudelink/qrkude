// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0

// QRKude for Flutter: a gallery of styles and a small live editor.

import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:qrkude_flutter/qrkude_flutter.dart';

void main() => runApp(const QRKudeExampleApp());

class QRKudeExampleApp extends StatelessWidget {
  const QRKudeExampleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'QRKude',
      theme: ThemeData(colorSchemeSeed: const Color(0xFF1D3557), useMaterial3: true),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  int _tab = 0;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('QRKude')),
      body: _tab == 0 ? const GalleryPage() : const EditorPage(),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tab,
        onDestinationSelected: (i) => setState(() => _tab = i),
        destinations: const [
          NavigationDestination(icon: Icon(Icons.grid_view), label: 'Gallery'),
          NavigationDestination(icon: Icon(Icons.tune), label: 'Editor'),
        ],
      ),
    );
  }
}

// ---- gallery ----------------------------------------------------------------

/// Gallery entries use the cross-language JSON format, exactly as another
/// QRKude port (or a backend) would store them.
const List<(String, Map<String, Object?>)> galleryStyles = [
  ('classic', {}),
  (
    'dots',
    {
      'modules': {'shape': 'dot', 'size': 0.9},
      'eyes': {'frame': 'circle', 'ball': 'circle'},
    }
  ),
  (
    'fluid gradient',
    {
      'modules': {
        'shape': 'fluid',
        'color': {
          'type': 'linear',
          'angle': 45,
          'stops': [
            {'offset': 0, 'color': '#6a11cb'},
            {'offset': 1, 'color': '#2575fc'},
          ],
        },
      },
      'eyes': {'frame': 'rounded', 'ball': 'circle', 'frameColor': '#e63946'},
    }
  ),
  (
    'liquid',
    {
      'modules': {'shape': 'liquid', 'color': '#1d3557'},
      'eyes': {'frame': 'circle', 'ball': 'circle'},
    }
  ),
  (
    'classy leaf',
    {
      'modules': {'shape': 'classy'},
      'eyes': {'frame': 'leaf', 'ball': 'leaf'},
    }
  ),
  (
    'radial dots',
    {
      'modules': {
        'shape': 'dot',
        'color': {
          'type': 'radial',
          'stops': [
            {'offset': 0, 'color': '#0b3d91'},
            {'offset': 1, 'color': '#00a896'},
          ],
        },
      },
      'eyes': {'frame': 'circle', 'ball': 'circle', 'ballColor': '#e63946'},
    }
  ),
  (
    'bars',
    {
      'modules': {'shape': 'vbars', 'size': 0.75},
      'eyes': {'frame': 'drop', 'ball': 'drop'},
    }
  ),
  (
    'stars',
    {
      'modules': {'shape': 'star'},
      'eyes': {'frame': 'circle', 'ball': 'circle'},
    }
  ),
  (
    'dark',
    {
      'modules': {'shape': 'liquid', 'color': '#f1faee'},
      'background': {'color': '#1d3557', 'radius': 4},
    }
  ),
  (
    'frame + label',
    {
      'margin': 2,
      'modules': {'shape': 'liquid'},
      'background': {'color': '#fff8e7', 'radius': 3},
      'border': {'width': 1, 'color': '#1d3557'},
      'label': {'text': 'SCAN ME', 'color': '#1d3557'},
    }
  ),
  (
    'logo',
    {
      'modules': {'shape': 'fluid'},
      'eyes': {'frame': 'rounded', 'ball': 'rounded'},
      'logo': {'size': 0.22, 'shape': 'circle', 'background': '#ffffff'},
    }
  ),
  (
    'alignment',
    {
      'modules': {'shape': 'dot'},
      'eyes': {'frame': 'circle', 'ball': 'circle'},
      'alignment': {'frame': 'circle', 'ball': 'circle'},
    }
  ),
];

class GalleryPage extends StatelessWidget {
  const GalleryPage({super.key});

  @override
  Widget build(BuildContext context) {
    return GridView.extent(
      maxCrossAxisExtent: 220,
      padding: const EdgeInsets.all(12),
      mainAxisSpacing: 12,
      crossAxisSpacing: 12,
      childAspectRatio: 0.8,
      children: [
        for (final (name, style) in galleryStyles)
          Card(
            clipBehavior: Clip.antiAlias,
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: Column(
                children: [
                  Expanded(
                    child: Center(
                      child: QRKudeView(
                        data: 'https://kudelink.com/${name.replaceAll(' ', '-')}',
                        style: style,
                        logo: style.containsKey('logo') ? const _LogoImage() : null,
                        semanticsLabel: 'QR code, $name style',
                      ),
                    ),
                  ),
                  const SizedBox(height: 8),
                  Text(name, style: Theme.of(context).textTheme.labelLarge),
                ],
              ),
            ),
          ),
      ],
    );
  }
}

// ---- editor -----------------------------------------------------------------

const List<int> _swatches = [
  0x000000FF,
  0x1D3557FF,
  0x0B3D91FF,
  0x6A11CBFF,
  0x00A896FF,
  0x2A9D8FFF,
  0xE63946FF,
  0xF4A261FF,
  0xFFFFFFFF,
  0xFDFCDCFF,
  0xF1FAEEFF,
];

class EditorPage extends StatefulWidget {
  const EditorPage({super.key});

  @override
  State<EditorPage> createState() => _EditorPageState();
}

class _EditorPageState extends State<EditorPage> {
  final _data = TextEditingController(text: 'https://kudelink.com');
  final _label = TextEditingController();

  ModuleShape _shape = ModuleShape.fluid;
  EyeShape _frame = EyeShape.rounded;
  EyeShape _ball = EyeShape.circle;
  double _size = 1;
  double _roundness = 1;
  int _color = 0x1D3557FF;
  int _eyeColor = 0xE63946FF;
  int _background = 0xFFFFFFFF;
  bool _gradient = false;
  bool _logo = false;
  LabelPosition _labelPosition = LabelPosition.bottom;

  @override
  void dispose() {
    _data.dispose();
    _label.dispose();
    super.dispose();
  }

  QRKudeStyle get _style => QRKudeStyle(
        modules: QRKudeModules(
          shape: _shape,
          size: _size,
          roundness: _roundness,
          color: _gradient
              ? QRKudeLinearGradient(angle: 45, stops: [
                  QRKudeGradientStop(0, _color),
                  QRKudeGradientStop(1, _eyeColor),
                ])
              : QRKudeSolid(_color),
        ),
        eyes: QRKudeEyes(frame: _frame, ball: _ball, frameColor: QRKudeSolid(_eyeColor)),
        background: QRKudeBackground(color: QRKudeSolid(_background), radius: 2),
        logo: _logo ? const QRKudeLogo(size: 0.22, shape: LogoShape.circle, background: QRKudeColor.white) : null,
        label: _label.text.isEmpty
            ? null
            : QRKudeLabel(text: _label.text, color: _color, position: _labelPosition, size: 3),
      );

  @override
  Widget build(BuildContext context) {
    final style = _style;
    final preview = _Preview(data: _data.text, style: style, logo: _logo);
    final controls = _controls(context);
    return LayoutBuilder(
      builder: (context, constraints) {
        if (constraints.maxWidth > 760) {
          return Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Expanded(child: SingleChildScrollView(padding: const EdgeInsets.all(16), child: preview)),
              SizedBox(
                width: 380,
                child: ListView(padding: const EdgeInsets.all(16), children: controls),
              ),
            ],
          );
        }
        return ListView(padding: const EdgeInsets.all(16), children: [preview, const Divider(), ...controls]);
      },
    );
  }

  List<Widget> _controls(BuildContext context) {
    return [
      TextField(
        controller: _data,
        decoration: const InputDecoration(labelText: 'Data'),
        onChanged: (_) => setState(() {}),
      ),
      const SizedBox(height: 12),
      _dropdown<ModuleShape>('Modules', _shape, ModuleShape.values, (v) => _shape = v),
      _dropdown<EyeShape>('Eye frame', _frame, EyeShape.values, (v) => _frame = v),
      _dropdown<EyeShape>('Eye ball', _ball, EyeShape.values, (v) => _ball = v),
      _slider('Size', _size, 0.1, 1, (v) => _size = v),
      _slider('Roundness', _roundness, 0, 1, (v) => _roundness = v),
      _swatchRow('Modules color', _color, (v) => _color = v),
      _swatchRow('Eye color', _eyeColor, (v) => _eyeColor = v),
      _swatchRow('Background', _background, (v) => _background = v),
      SwitchListTile(
        contentPadding: EdgeInsets.zero,
        title: const Text('Gradient (modules color to eye color)'),
        value: _gradient,
        onChanged: (v) => setState(() => _gradient = v),
      ),
      SwitchListTile(
        contentPadding: EdgeInsets.zero,
        title: const Text('Logo'),
        value: _logo,
        onChanged: (v) => setState(() => _logo = v),
      ),
      TextField(
        controller: _label,
        decoration: const InputDecoration(labelText: 'Label (empty = none)'),
        onChanged: (_) => setState(() {}),
      ),
      const SizedBox(height: 8),
      SegmentedButton<LabelPosition>(
        segments: const [
          ButtonSegment(value: LabelPosition.bottom, label: Text('Bottom')),
          ButtonSegment(value: LabelPosition.top, label: Text('Top')),
        ],
        selected: {_labelPosition},
        onSelectionChanged: (s) => setState(() => _labelPosition = s.first),
      ),
      const SizedBox(height: 16),
      FilledButton.icon(
        icon: const Icon(Icons.image_outlined),
        label: const Text('Export PNG (1024 px)'),
        onPressed: () => _exportPng(context),
      ),
    ];
  }

  /// Renders the current code with [qrkudeToPngBytes]; a real app would
  /// save or share the bytes.
  Future<void> _exportPng(BuildContext context) async {
    final messenger = ScaffoldMessenger.of(context);
    try {
      final Uint8List png = await qrkudeToPngBytes(_data.text, style: _style, width: 1024);
      messenger.showSnackBar(SnackBar(content: Text('PNG ready: ${png.length} bytes')));
    } on QRKudeException catch (e) {
      messenger.showSnackBar(SnackBar(content: Text(e.message)));
    }
  }

  Widget _dropdown<T extends Enum>(String label, T value, List<T> values, void Function(T) onChanged) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: DropdownButtonFormField<T>(
        initialValue: value,
        decoration: InputDecoration(labelText: label),
        items: [for (final v in values) DropdownMenuItem(value: v, child: Text(v.name))],
        onChanged: (v) {
          if (v != null) setState(() => onChanged(v));
        },
      ),
    );
  }

  Widget _slider(String label, double value, double min, double max, void Function(double) onChanged) {
    return Row(
      children: [
        SizedBox(width: 90, child: Text(label)),
        Expanded(
          child: Slider(
            value: value,
            min: min,
            max: max,
            divisions: 18,
            label: value.toStringAsFixed(2),
            onChanged: (v) => setState(() => onChanged(v)),
          ),
        ),
      ],
    );
  }

  Widget _swatchRow(String label, int selected, void Function(int) onChanged) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label),
          const SizedBox(height: 4),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (final c in _swatches)
                Semantics(
                  button: true,
                  selected: c == selected,
                  label: QRKudeColor.toHex(c),
                  child: InkWell(
                    customBorder: const CircleBorder(),
                    onTap: () => setState(() => onChanged(c)),
                    child: Container(
                      width: 28,
                      height: 28,
                      decoration: BoxDecoration(
                        color: qrkudeColorToColor(c),
                        shape: BoxShape.circle,
                        border: Border.all(
                          color: c == selected ? Theme.of(context).colorScheme.primary : Colors.black26,
                          width: c == selected ? 3 : 1,
                        ),
                      ),
                    ),
                  ),
                ),
            ],
          ),
        ],
      ),
    );
  }
}

class _Preview extends StatelessWidget {
  const _Preview({required this.data, required this.style, required this.logo});

  final String data;
  final QRKudeStyle style;
  final bool logo;

  @override
  Widget build(BuildContext context) {
    final doc = QRKudeDocument(style: style);
    List<String> flags;
    String info;
    try {
      final code = doc.encodeText(data);
      flags = lint(code, style);
      info = 'version ${code.version}, ECC ${code.ecc.letter}, mask ${code.mask}, ${code.mode.name}';
    } on QRKudeException catch (e) {
      flags = const [];
      info = e.message;
    }
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 360),
            child: QRKudeView(
              data: data,
              style: style,
              logo: logo ? const _LogoImage() : null,
              errorBuilder: (context, error) => SizedBox(
                height: 200,
                child: Center(child: Text('$error', style: TextStyle(color: theme.colorScheme.error))),
              ),
            ),
          ),
        ),
        const SizedBox(height: 12),
        Text(info, textAlign: TextAlign.center, style: theme.textTheme.bodySmall),
        const SizedBox(height: 8),
        Wrap(
          alignment: WrapAlignment.center,
          spacing: 6,
          runSpacing: 6,
          children: [
            if (flags.isEmpty) const Chip(avatar: Icon(Icons.check, size: 16), label: Text('no lint warnings')),
            for (final f in flags) Chip(avatar: const Icon(Icons.warning_amber, size: 16), label: Text(f)),
          ],
        ),
        const SizedBox(height: 12),
        ExpansionTile(
          title: const Text('Style JSON (same format in every QRKude port)'),
          children: [
            SelectableText(
              const JsonEncoder.withIndent('  ').convert(style.toJson()),
              style: const TextStyle(fontFamily: 'monospace', fontSize: 12),
            ),
          ],
        ),
      ],
    );
  }
}

// ---- logo -------------------------------------------------------------------

/// A demo logo drawn at runtime (no asset needed): a red disc with a white
/// rounded square. Equal instances share the image cache entry.
class _LogoImage extends ImageProvider<_LogoImage> {
  const _LogoImage();

  @override
  Future<_LogoImage> obtainKey(ImageConfiguration configuration) => Future.value(this);

  @override
  ImageStreamCompleter loadImage(_LogoImage key, ImageDecoderCallback decode) => OneFrameImageStreamCompleter(_draw());

  static Future<ImageInfo> _draw() async {
    const size = 128.0;
    final recorder = ui.PictureRecorder();
    final canvas = Canvas(recorder);
    canvas.drawCircle(const Offset(size / 2, size / 2), size / 2 - 2, Paint()..color = const Color(0xFFE63946));
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromCenter(center: const Offset(size / 2, size / 2), width: size * 0.42, height: size * 0.42),
        const Radius.circular(8),
      ),
      Paint()..color = Colors.white,
    );
    final image = await recorder.endRecording().toImage(size.toInt(), size.toInt());
    return ImageInfo(image: image);
  }

  @override
  bool operator ==(Object other) => other is _LogoImage;

  @override
  int get hashCode => 0x10C0;
}
