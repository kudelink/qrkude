/*
 * QRKude - <qr-kude> web component (entry "qrkude/element").
 *
 *   <qr-kude value="https://kudelink.com" module-shape="fluid" color="#1d3557"></qr-kude>
 *   <qr-kude value="..." config='{"style": {"modules": {"shape": "dot"}}}'></qr-kude>
 *
 * Importing this module registers the element as `qr-kude` (when
 * `customElements` exists).
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
import {
  DocumentInput,
  EYE_SHAPES,
  EyeShape,
  LintFlag,
  MODULE_SHAPES,
  ModuleShape,
  QRCode,
  QRDocument,
  encodeForStyle,
  lint,
  logoDamage,
  parseColor,
  parseDocument,
  solidPaint,
  toSvg,
} from './index.js';

const ATTRIBUTES = [
  'value',
  'config',
  'module-shape',
  'eye-frame',
  'eye-ball',
  'color',
  'background',
  'logo',
  'label',
  'ecc',
  'margin',
  'size',
] as const;

/** Detail of the `qrkude-render` event. */
export interface QRKudeRenderDetail {
  svg: string;
  code: QRCode;
  lint: LintFlag[];
  logoDamage: number;
}

const Base: typeof HTMLElement =
  typeof HTMLElement !== 'undefined' ? HTMLElement : (class {} as unknown as typeof HTMLElement);

const STYLE = `:host{display:inline-block;width:256px;line-height:0;vertical-align:middle}
:host([hidden]){display:none}
.qr{width:100%}
svg{display:block;width:100%;height:auto}`;

function warn(message: string): void {
  if (typeof console !== 'undefined') console.warn(`<qr-kude>: ${message}`);
}

/**
 * Renders a styled QR code as inline SVG (in its shadow root) and re-renders
 * whenever an attribute changes.
 *
 * Attributes: `value`, `config` (JSON document `{encode, style, svg}` or a bare
 * style), and the shortcuts `module-shape`, `eye-frame`, `eye-ball`, `color`,
 * `background`, `logo` (image URL), `label`, `ecc`, `margin`, `size` (CSS px).
 *
 * Events: `qrkude-render` ({@link QRKudeRenderDetail}), `qrkude-error`
 * (`{ error }`).
 */
export class QRKudeElement extends Base {
  static get observedAttributes(): string[] {
    return ATTRIBUTES.slice();
  }

  /** Last rendered SVG markup ("" before the first render or after an error). */
  svg = '';
  /** Last encoded symbol. */
  code: QRCode | null = null;
  /** Lint flags of the last render. */
  lintFlags: LintFlag[] = [];
  /** Last error, if the last render failed. */
  error: Error | null = null;

  private configValue: DocumentInput | null = null;
  private queued = false;
  private readonly container: HTMLElement;
  private readonly sizeRule: HTMLStyleElement;

  constructor() {
    super();
    const root = this.attachShadow({ mode: 'open' });
    const style = document.createElement('style');
    style.textContent = STYLE;
    this.sizeRule = document.createElement('style');
    this.container = document.createElement('div');
    this.container.className = 'qr';
    this.container.setAttribute('part', 'code');
    this.container.setAttribute('role', 'img');
    root.append(style, this.sizeRule, this.container);
  }

  connectedCallback(): void {
    this.render();
  }

  attributeChangedCallback(): void {
    this.schedule();
  }

  /** The encoded text. */
  get value(): string {
    return this.getAttribute('value') || '';
  }

  set value(v: string) {
    this.setAttribute('value', v);
  }

  /** The style document. Strings are reflected to the `config` attribute;
   * objects are kept as a property. */
  get config(): DocumentInput | null {
    return this.configValue !== null ? this.configValue : this.getAttribute('config');
  }

  set config(v: DocumentInput | null) {
    if (typeof v === 'string' || v === null) {
      this.configValue = null;
      if (v === null) this.removeAttribute('config');
      else this.setAttribute('config', v);
    } else {
      this.configValue = v;
      this.schedule();
    }
  }

  private schedule(): void {
    if (this.queued) return;
    this.queued = true;
    Promise.resolve().then(() => {
      this.queued = false;
      if (this.isConnected) this.render();
    });
  }

  /** Builds the document from `config` plus the shortcut attributes. */
  document(): QRDocument {
    const doc = parseDocument(this.config || null);
    const s = doc.style;
    const attr = (name: string): string | null => this.getAttribute(name);
    const shape = attr('module-shape');
    if (shape) {
      if ((MODULE_SHAPES as readonly string[]).indexOf(shape) >= 0) s.modules.shape = shape as ModuleShape;
      else warn(`unknown module-shape "${shape}"`);
    }
    for (const [name, key] of [['eye-frame', 'frame'], ['eye-ball', 'ball']] as const) {
      const v = attr(name);
      if (!v) continue;
      if ((EYE_SHAPES as readonly string[]).indexOf(v) >= 0) s.eyes[key] = v as EyeShape;
      else warn(`unknown ${name} "${v}"`);
    }
    const color = attr('color');
    if (color) {
      const c = parseColor(color.trim());
      if (c !== null) s.modules.color = solidPaint(c);
      else warn(`invalid color "${color}"`);
    }
    const background = attr('background');
    if (background) {
      if (background === 'none') s.background.color = { type: 'none' };
      else {
        const c = parseColor(background.trim());
        if (c !== null) s.background.color = solidPaint(c);
        else warn(`invalid background "${background}"`);
      }
    }
    const logo = attr('logo');
    if (logo) {
      s.logo.href = logo;
      if (!(s.logo.size > 0)) s.logo.size = 0.2;
    }
    const label = attr('label');
    if (label !== null) s.label.text = label;
    const margin = attr('margin');
    if (margin !== null && margin.trim() !== '' && isFinite(Number(margin))) s.margin = Number(margin);
    const ecc = attr('ecc');
    if (ecc) {
      if (ecc === 'L' || ecc === 'M' || ecc === 'Q' || ecc === 'H') doc.encode.ecc = ecc;
      else warn(`invalid ecc "${ecc}"`);
    }
    return doc;
  }

  /** Renders synchronously (normally done automatically). */
  render(): void {
    const size = this.getAttribute('size');
    const px = size !== null && isFinite(parseFloat(size)) ? parseFloat(size) : 0;
    this.sizeRule.textContent = px > 0 ? `:host{width:${px}px}` : '';
    try {
      const doc = this.document();
      const code = encodeForStyle(this.value, doc.encode, doc.style);
      const markup = toSvg(code, doc.style, doc.svg);
      this.code = code;
      this.svg = markup;
      this.error = null;
      this.lintFlags = lint(code, doc.style);
      this.container.innerHTML = markup;
      this.container.setAttribute('aria-label', `QR code: ${this.value}`);
      const detail: QRKudeRenderDetail = {
        svg: markup,
        code,
        lint: this.lintFlags,
        logoDamage: logoDamage(code, doc.style),
      };
      this.dispatchEvent(new CustomEvent('qrkude-render', { detail }));
    } catch (err) {
      this.code = null;
      this.svg = '';
      this.lintFlags = [];
      this.error = err instanceof Error ? err : new Error(String(err));
      this.container.innerHTML = '';
      this.dispatchEvent(new CustomEvent('qrkude-error', { detail: { error: this.error } }));
    }
  }
}

/** Registers the element under `name` (default `qr-kude`). Safe to call twice. */
let baseDefined = false;

export function defineQRKudeElement(name = 'qr-kude'): void {
  if (typeof customElements === 'undefined' || customElements.get(name)) return;
  /* a constructor can only be registered once: use a subclass for extra names */
  customElements.define(name, baseDefined ? class extends QRKudeElement {} : QRKudeElement);
  baseDefined = true;
}

defineQRKudeElement();
