/*
 * QRKude - ESP32_WebServer_SVG
 * Copyright 2026 The QRKude Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Serves styled QR codes as SVG over HTTP. The SVG is streamed straight
 * into the TCP connection, so even a version 40 code needs no big buffer.
 *
 *   http://<ip>/                      a small form
 *   http://<ip>/qr.svg?text=Hello&shape=fluid&frame=rounded&ball=circle
 *                     &fg=%23224488&bg=%23ffffff&width=320
 *   http://<ip>/qr.svg?text=Hi&style={"modules":{"shape":"liquid"}}
 *
 * shape: square dot rounded diamond star fluid liquid bevel classy hbars vbars
 * frame / ball: square rounded circle leaf drop bevel diamond dots
 * Also builds on ESP8266 (ESP8266WebServer).
 */
#include <QRKude.h>

#if defined(ESP8266)
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
ESP8266WebServer server(80);
#else
#include <WebServer.h>
#include <WiFi.h>
WebServer server(80);
#endif

#ifndef ARDUINO_BOARD
#define ARDUINO_BOARD "ESP32"
#endif

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";

/* "#rrggbb", "rrggbb", "#rgb"... -> color; false if invalid. */
static bool parseColor(const String &arg, qrk_color &out)
{
    String s = arg.startsWith("#") ? arg : "#" + arg;
    return qrk_parse_color(s.c_str(), s.length(), &out);
}

static void badRequest(const String &message)
{
    server.send(400, "text/plain", message + "\n");
}

static void handleSvg()
{
    QRKude qr; /* per request: the memory is released when it returns */

    if (server.hasArg("style") && !qr.setStyleJson(server.arg("style").c_str()))
        return badRequest(String("style: ") + qr.errorString());
    if (server.hasArg("shape")) {
        int shape = qrk_module_shape_from_name(server.arg("shape").c_str());
        if (shape < 0)
            return badRequest("unknown shape");
        qr.style().modules.shape = (uint8_t)shape;
    }
    if (server.hasArg("frame")) {
        int frame = qrk_eye_shape_from_name(server.arg("frame").c_str());
        if (frame < 0)
            return badRequest("unknown frame");
        qr.style().eyes.frame = (uint8_t)frame;
    }
    if (server.hasArg("ball")) {
        int ball = qrk_eye_shape_from_name(server.arg("ball").c_str());
        if (ball < 0)
            return badRequest("unknown ball");
        qr.style().eyes.ball = (uint8_t)ball;
    }
    qrk_color color;
    if (server.hasArg("fg")) {
        if (!parseColor(server.arg("fg"), color))
            return badRequest("bad fg color");
        qr.style().modules.paint = qrk_paint_solid(color);
    }
    if (server.hasArg("bg")) {
        if (!parseColor(server.arg("bg"), color))
            return badRequest("bad bg color");
        qr.style().background.paint = qrk_paint_solid(color);
    }
    if (server.hasArg("width"))
        qr.svgOptions().width = server.arg("width").toInt();

    String text = server.hasArg("text") ? server.arg("text") : String("https://kudelink.com");
    if (!qr.encode(text))
        return badRequest(String("encode: ") + qr.errorString());

    /* Content-Length first, then the document streamed in small chunks. */
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(qr.svgLength());
    server.send(200, "image/svg+xml", "");
    WiFiClient client = server.client();
    qr.printSvg(client);
}

static void handleRoot()
{
    String page;
    page.reserve(1500);
    page += F("<!doctype html><html><head><meta name=viewport content='width=device-width'>"
              "<title>QRKude</title></head><body style='font-family:sans-serif'>"
              "<h1>QRKude on ");
    page += ARDUINO_BOARD;
    page += F("</h1><form action='/' method='get'>Text <input name=text size=40 value='");
    String text = server.hasArg("text") ? server.arg("text") : String("https://kudelink.com");
    String value = text; /* HTML attribute escaping */
    value.replace("&", "&amp;");
    value.replace("'", "&#39;");
    value.replace("<", "&lt;");
    page += value;
    page += F("'> Shape <select name=shape>");
    String shape = server.hasArg("shape") ? server.arg("shape") : String("fluid");
    for (int i = 0; i < QRK_MODULE_SHAPE_COUNT; i++) {
        String name = qrk_module_shape_name(i);
        page += "<option" + String(name == shape ? " selected" : "") + ">" + name + "</option>";
    }
    page += F("</select> <button>Show</button></form><p><img width=320 src='/qr.svg?frame=rounded&ball=circle&shape=");
    page += shape;
    page += F("&text=");
    for (size_t i = 0; i < text.length(); i++) { /* percent-encode the payload */
        char c = text[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            page += c;
        } else {
            char hex[4];
            snprintf(hex, sizeof hex, "%%%02X", (unsigned char)c);
            page += hex;
        }
    }
    page += F("'></p></body></html>");
    server.send(200, "text/html", page);
}

void setup()
{
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print(F("Connecting"));
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();
    Serial.print(F("Open http://"));
    Serial.print(WiFi.localIP());
    Serial.println(F("/"));

    /* The address as a QR code in the serial monitor. */
    QRKude qr;
    String url = "http://" + WiFi.localIP().toString() + "/";
    if (qr.encode(url))
        qr.printAscii(Serial, QRKude::ASCII_HALF_BLOCKS, true);

    server.on("/", handleRoot);
    server.on("/qr.svg", handleSvg);
    server.begin();
}

void loop()
{
    server.handleClient();
}
