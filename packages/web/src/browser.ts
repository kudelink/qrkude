/*
 * QRKude - entry of the browser bundle (dist/qrkude.min.js, global `QRKude`).
 * Includes the library and registers the <qr-kude> element.
 *
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 */
export * from './index.js';
export { QRKudeElement, defineQRKudeElement } from './element.js';
export type { QRKudeRenderDetail } from './element.js';
