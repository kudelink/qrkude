#!/usr/bin/env python3
# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
"""Generates the conformance vectors in spec/vectors/ from the C reference
implementation. Every port must reproduce these outputs byte for byte.

    make -C core && python3 spec/tools/gen_vectors.py
"""
import json
import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLI = os.path.join(ROOT, "core", "build", "qrkude")
PAYLOAD_CLI = os.path.join(ROOT, "core", "build", "qrkude-payload")
OUT = os.path.join(ROOT, "spec", "vectors")

URL = "https://kudelink.com"
LOGO = "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg'/>"

MODULE_SHAPES = ["square", "dot", "rounded", "diamond", "star", "fluid", "liquid",
                 "bevel", "classy", "hbars", "vbars"]
EYE_SHAPES = ["square", "rounded", "circle", "leaf", "drop", "bevel", "diamond", "dots"]


def lin(angle, *colors):
    n = len(colors)
    return {"type": "linear", "angle": angle,
            "stops": [{"offset": round(i / (n - 1), 3), "color": c} for i, c in enumerate(colors)]}


def render_cases():
    c = []

    def add(name, style, text=URL, encode=None, svg=None):
        case = {"name": name, "text": text, "style": style}
        if encode is not None:
            case["encode"] = encode
        if svg is not None:
            case["svg"] = svg
        c.append(case)

    add("default", {}, "QRKude")
    add("empty-text", {}, "")
    add("unicode-text", {"modules": {"shape": "dot"}}, "Kude ñ € 😀")
    for shape in MODULE_SHAPES:
        add(f"module-{shape}", {"modules": {"shape": shape}})
    variants = [
        ("square", {"size": 0.7}), ("dot", {"size": 0.6}), ("rounded", {"size": 0.8, "roundness": 0.3}),
        ("diamond", {"size": 0.9}), ("star", {"size": 0.8}), ("fluid", {"roundness": 0.5}),
        ("liquid", {"roundness": 0.6}), ("bevel", {"roundness": 0.4}), ("classy", {"roundness": 0.7}),
        ("hbars", {"size": 0.6, "roundness": 0.5}), ("hbars", {"size": 1}), ("vbars", {"size": 0.9, "roundness": 0}),
        ("liquid", {"roundness": 0}),
    ]
    for i, (shape, params) in enumerate(variants):
        add(f"module-{shape}-variant{i}", {"modules": dict(shape=shape, **params)})
    for i, frame in enumerate(EYE_SHAPES):
        ball = EYE_SHAPES[(i + 3) % len(EYE_SHAPES)]
        add(f"eyes-{frame}-{ball}", {"eyes": {"frame": frame, "ball": ball}})
        add(f"eyes-{frame}-{frame}", {"eyes": {"frame": frame, "ball": frame}})
    v7 = "https://kudelink.com/" + "alignment-patterns-" * 6
    add("alignment-rounded-dots", {"modules": {"shape": "dot"},
        "alignment": {"frame": "rounded", "ball": "dots"}}, v7)
    add("alignment-leaf-diamond", {"eyes": {"frame": "leaf", "ball": "leaf"},
        "alignment": {"frame": "leaf", "ball": "diamond"}}, v7)
    add("alignment-dots-frame", {"alignment": {"frame": "dots", "ball": "circle"}}, "alignment v2 pattern")
    add("alignment-frame-only", {"alignment": {"frame": "circle"}}, "alignment v2 pattern")
    add("alignment-null", {"alignment": {"frame": None, "ball": None}}, "alignment v2 pattern")

    for angle in [0, 30, 90, 135, 180, 270, -45, 405, 12.5]:
        add(f"linear-{angle}", {"modules": {"shape": "fluid", "color": lin(angle, "#ff0000", "#0000ff")}})
    add("linear-3stops", {"modules": {"color": lin(60, "#ff0000", "#00ff00", "#0000ff")}})
    add("radial-default", {"modules": {"shape": "dot", "color": {"type": "radial", "stops": [
        {"offset": 0, "color": "#123456"}, {"offset": 1, "color": "#abcdef"}]}}})
    add("radial-custom", {"modules": {"color": {"type": "radial", "cx": 0.3, "cy": 0.7, "r": 0.9, "stops": [
        {"offset": 0.2, "color": "#ff000080"}, {"offset": 0.8, "color": "#00ff00"}]}}})
    add("alpha-colors", {"modules": {"color": "#11223380"}, "background": {"color": "#ffffff40"}})
    add("short-colors", {"modules": {"color": "#a1c"}, "background": {"color": "#FED"}})
    add("eye-paints", {"modules": {"color": "#222222"}, "eyes": {"frame": "rounded", "ball": "circle",
        "frameColor": lin(90, "#e63946", "#1d3557"), "ballColor": "#2a9d8f"}})
    add("eye-paints-inherit-gradient", {"modules": {"color": lin(45, "#6a11cb", "#2575fc")},
        "eyes": {"frameColor": None, "ballColor": "#ff0000"}})
    add("background-gradient", {"background": {"color": lin(90, "#ffffff", "#e0f7fa"), "radius": 2}})
    add("background-radial", {"background": {"color": {"type": "radial", "stops": [
        {"offset": 0, "color": "#ffffff"}, {"offset": 1, "color": "#dddddd"}]}}})
    add("background-transparent", {"background": {"color": None}})
    add("background-transparent-string", {"background": {"color": "transparent"}})
    add("background-radius", {"background": {"radius": 3.5}})
    add("background-radius-clamped", {"background": {"radius": 1000}})
    add("border-1", {"border": {"width": 1, "color": "#1d3557"}})
    add("border-radius", {"margin": 2, "background": {"radius": 3}, "border": {"width": 0.5, "color": "#ff000080"}})
    add("border-thick-radius", {"background": {"radius": 2}, "border": {"width": 3}})

    for shape in ["square", "rounded", "circle"]:
        add(f"logo-{shape}", {"modules": {"shape": "fluid"}, "logo": {"href": LOGO, "shape": shape,
            "background": "#ffffff"}}, "https://kudelink.com/logo/" + shape)
    add("logo-no-excavate", {"logo": {"href": LOGO, "excavate": False, "size": 0.3}}, "https://kudelink.com/logo")
    add("logo-no-href", {"logo": {"size": 0.25, "margin": 0, "background": "#f1faee"}}, "https://kudelink.com/logo")
    add("logo-escape", {"logo": {"href": "https://x.test/a.png?a=1&b=\"2\"<>", "size": 0.15,
        "margin": 2}}, "https://kudelink.com/logo")
    add("logo-null", {"logo": None})
    add("logo-ecc-explicit", {"logo": {"href": LOGO}}, "https://kudelink.com/logo", encode={"ecc": "L"})
    add("logo-liquid-circle", {"modules": {"shape": "liquid"}, "logo": {"href": LOGO, "shape": "circle",
        "size": 0.27, "margin": 0.5}}, "https://kudelink.com/" + "logo" * 20)

    add("label-bottom", {"label": {"text": "SCAN ME"}})
    add("label-top", {"label": {"text": "Top label", "position": "top", "bold": False, "size": 2.5,
        "color": "#e6394680", "font": "Georgia, \"Times New Roman\", serif"}})
    add("label-escape", {"label": {"text": "<Kude & \"QR\">"}})
    add("label-empty", {"label": {"text": ""}})
    add("label-frame", {"margin": 2, "modules": {"shape": "liquid"}, "background": {"color": "#fff8e7", "radius": 3},
        "border": {"width": 1, "color": "#1d3557"}, "label": {"text": "SCAN ME", "color": "#1d3557"}})
    add("label-top-border", {"border": {"width": 2}, "label": {"text": "TOP", "position": "top"}})

    add("svg-width", {}, svg={"width": 300})
    add("svg-width-label", {"label": {"text": "w"}}, svg={"width": 256, "idPrefix": "custom"})
    add("svg-prefix-gradient", {"modules": {"color": lin(45, "#000000", "#ff0000")}}, svg={"idPrefix": "q-1"})
    add("margin-0", {"margin": 0})
    add("margin-2.5", {"margin": 2.5})
    add("margin-negative", {"margin": -3})
    add("clamp-size-low", {"modules": {"shape": "dot", "size": 0}})
    add("clamp-size-high", {"modules": {"shape": "dot", "size": 2}})
    add("clamp-roundness", {"modules": {"shape": "fluid", "roundness": -1}})
    add("clamp-logo-size", {"logo": {"size": 1.5, "margin": -1}})
    add("encode-L-mask3", {}, encode={"ecc": "L", "mask": 3, "boostEcc": False})
    add("encode-minversion", {"modules": {"shape": "dot"}}, encode={"minVersion": 5})
    add("encode-eci", {}, "ñandú", encode={"eci": True})
    add("unknown-keys-ignored", {"modules": {"shape": "dot", "futureOption": [1, {"x": None}]}, "future": True})
    add("kitchen-sink", {
        "margin": 3,
        "modules": {"shape": "liquid", "roundness": 0.9, "color": lin(135, "#0b3d91", "#00a896", "#f4a261")},
        "eyes": {"frame": "leaf", "ball": "drop", "frameColor": "#0b3d91", "ballColor": {"type": "radial",
                 "stops": [{"offset": 0, "color": "#e63946"}, {"offset": 1, "color": "#9d0208"}]}},
        "alignment": {"frame": "circle", "ball": "circle"},
        "background": {"color": "#fdfcdc", "radius": 4},
        "border": {"width": 1.5, "color": "#0b3d91"},
        "logo": {"href": LOGO, "size": 0.2, "shape": "rounded", "background": "#fdfcdc"},
        "label": {"text": "kudelink.com", "color": "#0b3d91"},
    }, "https://kudelink.com/kitchen-sink?utm_source=qr", svg={"width": 512, "idPrefix": "ks"})

    # --- hardening: untrusted documents (SPEC 2.0, 8.3, 11) ---
    grad = {"modules": {"color": lin(0, "#000000", "#112233")}}
    add("idprefix-injection", grad, svg={"idPrefix":
        'x"/><svg onload="globalThis.__qrkudeXss=1"></svg><linearGradient id="y'})
    add("idprefix-digit", grad, svg={"idPrefix": "1abc"})
    add("idprefix-dash", grad, svg={"idPrefix": "-x"})
    add("idprefix-unicode", grad, svg={"idPrefix": "ñ-ü"})
    add("idprefix-empty", grad, svg={"idPrefix": ""})
    add("clamp-huge-margin", {"margin": 1e6})
    add("clamp-huge-border-label", {"border": {"width": 5000}, "label": {"text": "big", "size": 1e9}})
    add("clamp-huge-logo-margin", {"logo": {"size": 0.1, "margin": 1e5}})
    add("lint-invisible-null", {"modules": {"color": None}})
    add("lint-invisible-alpha", {"modules": {"color": "#00000000"}, "eyes": {"frameColor": "#000000"}})
    add("lint-alpha-on-dark", {"modules": {"color": "#00000080"}, "background": {"color": "#000000"}})
    add("lint-gradient-background", {"background": {"color": lin(0, "#000000", "#ffffff")}})
    add("lint-transparent-gradient-background", {"background": {"color": lin(0, "#ffffff00", "#00000000")}})

    def raw(name, doc, text=URL):
        c.append({"name": name, "text": text, "json": doc})

    raw("json-duplicate-modules", '{"style":{"modules":{"shape":"dot"},"modules":{"size":0.8}}}')
    raw("json-duplicate-logo", '{"style":{"logo":{"size":0.3},"logo":{"href":"x.png"}}}')
    raw("json-logo-null-reset", '{"style":{"logo":{"size":0.3},"logo":null,"logo":{}}}')
    raw("json-duplicate-paint", '{"style":{"modules":{"color":{"type":"radial","stops":[{"offset":0,"color":"#f00"}]}},'
        '"modules":{"color":{"stops":[{"offset":0,"color":"#00f"},{"offset":1,"color":"#0f0"}]}}}}')
    raw("json-alignment-null", '{"style":{"alignment":{"frame":"circle"},"alignment":null}}', "alignment v2 pattern")
    raw("json-duplicate-encode", '{"encode":{"ecc":"L"},"encode":{"mask":3}}')
    raw("json-top-level-style-keys", '{"modules":{"shape":"fluid"},"style":{"eyes":{"frame":"circle"}}}')
    raw("json-escaped-keys", '{"encode":{"\\u0065cc":"L"},"style":{"m\\u006fdules":{"shape":"dot"}}}')
    raw("json-unknown-long-key", '{"style":{"' + "k" * 200 + '":{"' + "n" * 100 + '":[1,{"x":null}]},"margin":2}}')
    raw("json-control-characters", '{"style":{"label":{"text":"a\\u0001b\\u001fc\\td\\nend",'
        '"font":"x\\u0007y"},"logo":{"href":"data:,a\\u0002b","size":0.1}}}')
    raw("json-surrogates", '{"style":{"label":{"text":"lone \\ud800 high, \\udc00 low, pair \\ud83d\\ude00, '
        'swapped \\udc00\\ud800"}}}')
    raw("json-nul-in-string", '{"svg":{"idPrefix":"\\u0000x"},"style":{"modules":{"color":{"stops":['
        '{"offset":0,"color":"#000"}]}},"label":{"text":"ab\\u0000cd"}}}')
    raw("json-nul-in-values", '{"encode":{"ecc":"L\\u0000' + "x" * 40 + '"},"style":{"modules":{"shape":"dot\\u0000no",'
        '"color":"#f00\\u0000x"},"border":{"width":1,"color":"#00f\\u0000"},"label":{"text":"t","position":"top\\u0000x"}}}')
    raw("json-null-resets-logo", '{"style":{"logo":{"href":"a.png","size":0.3,"margin":3,"shape":"circle"},'
        '"logo":null,"logo":{}}}')
    raw("json-null-resets-label", '{"style":{"label":{"text":"A","size":2,"position":"top"},"label":null,'
        '"label":{"text":"B"}}}')
    raw("json-gradient-no-stops", '{"style":{"modules":{"color":{"type":"radial","stops":[]}},"eyes":{"frameColor":"#000"}}}')
    raw("json-unknown-ecc-key", '{"style":{"future":{"ecc":"L"},"logo":{"size":0.2}}}')
    raw("json-mask-truncate", '{"encode":{"mask":6.9}}')
    raw("json-mask-negative", '{"encode":{"mask":-1.7}}')
    raw("json-number-400-chars", '{"style":{"x":0.' + "1" * 398 + '}}')
    raw("json-depth-64", '{"style":{"x":' + "[" * 62 + "]" * 62 + '}}')
    raw("json-error-depth-65", '{"style":{"x":' + "[" * 63 + "]" * 63 + '}}')
    raw("json-error-number-401-chars", '{"style":{"x":0.' + "1" * 399 + '}}')
    raw("json-error-non-finite", '{"style":{"margin":1e999}}')
    raw("json-error-non-finite-unknown", '{"style":{"future":-1e999}}')
    raw("json-error-huge-exponent", '{"style":{"future":1e999999999999}}')
    raw("json-error-shape", '{"style":{"modules":{"shape":"nope"}}}')
    raw("json-error-min-version", '{"encode":{"minVersion":1e100}}')
    raw("json-error-max-version", '{"encode":{"maxVersion":41}}')
    return c


def encode_cases():
    c = []

    def add(text, **enc):
        c.append({"text": text, "encode": enc})

    add("")
    add("0")
    add("01234567")
    add("0" * 41, ecc="L", boostEcc=False)
    add("0123456789" * 4)
    add("HELLO WORLD")
    add("AC-42 $%*+-./:")
    add("hello world")
    add("https://kudelink.com/?q=1&x=ñ")
    add("😀 emoji ✓")
    add("ñandú", eci=True)
    for e in "LMQH":
        add("QRKude ECC " + e, ecc=e, boostEcc=False)
    for m in range(8):
        add("mask test", mask=m)
    for v in [7, 10, 14, 21, 27, 32, 36]:
        add("version " + str(v), minVersion=v)
    add("a" * 300)
    add("B" * 1000, ecc="Q")
    add("9" * 3000, ecc="H")
    add("x" * 2953, ecc="L")
    add("7" * 7089, ecc="L")
    add("Z" * 4296, ecc="L")
    add("x" * 2954, ecc="L")  # too long
    add("y" * 100, maxVersion=3)  # too long
    add("fits", minVersion=2, maxVersion=2, ecc="H")
    return c


def payload_cases():
    return [
        {"type": "wifi", "args": {"ssid": "Kude", "password": "secret", "auth": "WPA", "hidden": False}},
        {"type": "wifi", "args": {"ssid": "My;Net,\"x\":y\\", "password": "p@ss:word;", "auth": "WEP", "hidden": True}},
        {"type": "wifi", "args": {"ssid": "Open", "password": "ignored", "auth": "nopass", "hidden": False}},
        {"type": "wifi", "args": {"ssid": "NoAuth", "password": "", "auth": None, "hidden": False}},
        {"type": "email", "args": {"to": "hello@kudelink.com", "subject": "Hola qué tal", "body": "a&b=c/d?e #1"}},
        {"type": "email", "args": {"to": "a@b.c", "subject": None, "body": None}},
        {"type": "email", "args": {"to": "a@b.c", "subject": "", "body": "only~body_ok.-"}},
        {"type": "sms", "args": {"number": "+34600000000", "message": "Hola!"}},
        {"type": "sms", "args": {"number": "+1555", "message": None}},
        {"type": "tel", "args": {"number": "+34 600 000 000"}},
        {"type": "geo", "args": {"latitude": 40.4168, "longitude": -3.7038}},
        {"type": "geo", "args": {"latitude": 0, "longitude": 0}},
        {"type": "geo", "args": {"latitude": -33.8688197, "longitude": 151.2092955}},
        {"type": "geo", "args": {"latitude": 1.0000005, "longitude": -0.0000004}},
        {"type": "geo", "args": {"latitude": 95.5, "longitude": -200}},
        {"type": "geo", "args": {"latitude": -91, "longitude": 180.0000001}},
        {"type": "vcard", "args": {"firstName": "Ana", "lastName": "García", "organization": "Kude; Labs",
                                   "title": "CTO", "phone": "+34 600", "email": "ana@kudelink.com",
                                   "url": "https://kudelink.com", "address": "Calle 1, Madrid",
                                   "note": "line1\nline2 \\ end"}},
        {"type": "vcard", "args": {"firstName": "Solo"}},
        {"type": "vcard", "args": {"lastName": "Apellido", "phone": "123"}},
        {"type": "event", "args": {"summary": "Launch, v1; go", "start": "20261017T100000Z",
                                   "end": "20261017T120000Z", "location": "Online",
                                   "description": "Multi\nline"}},
        {"type": "event", "args": {"summary": "Minimal"}},
    ]


def run(args, text=None):
    r = subprocess.run(args, input=text.encode() if text is not None else None,
                       capture_output=True)
    return r.returncode, r.stdout.decode(), r.stderr.decode()


def info(text, doc):
    doc_text = doc if isinstance(doc, str) else json.dumps(doc)
    code, out, err = run([CLI, "--stdin", "-j", doc_text, "--info"], text)
    if code != 0:
        return {"error": err.strip().split(": ", 1)[-1]}
    d = {k: v.strip() for k, v in ((line + " ").split(" ", 1) for line in out.strip().splitlines())}
    return {"lint": d["lint"].split(),"version": int(d["version"]), "ecc": d["ecc"], "mask": int(d["mask"]),
            "mode": {"1": "numeric", "2": "alphanumeric", "4": "byte"}[d["mode"]],
            "logoDamage": float(d["logo_damage"]), "hasLogo": d.get("logo") == "1"}


def main():
    os.makedirs(OUT, exist_ok=True)

    renders = []
    for case in render_cases():
        if "json" in case:
            doc_text = case["json"]
        else:
            doc_text = json.dumps({k: case[k] for k in ("encode", "style", "svg") if k in case})
        meta = info(case["text"], doc_text)
        out = dict(case)
        if "error" in meta:
            assert case["name"].startswith("json-error-"), (case["name"], meta)
            out["error"] = meta["error"]
            renders.append(out)
            continue
        assert not case["name"].startswith("json-error-"), case["name"]
        code, svg, err = run([CLI, "--stdin", "-j", doc_text], case["text"])
        assert code == 0, (case["name"], err)
        out.update({k: meta[k] for k in ("version", "ecc", "mask", "lint")})
        if meta["hasLogo"]:
            out["logoDamage"] = meta["logoDamage"]
        out["output"] = svg
        renders.append(out)

    encodes = []
    for case in encode_cases():
        doc = {"encode": case["encode"]}
        meta = info(case["text"], doc)
        out = dict(case)
        if "error" in meta:
            out["error"] = "data too long"
        else:
            code, matrix, _ = run([CLI, "--stdin", "-j", json.dumps(doc), "--matrix"], case["text"])
            out.update({k: meta[k] for k in ("version", "ecc", "mask", "mode")})
            out["matrix"] = matrix.split()
        encodes.append(out)

    payloads = []
    order = {
        "wifi": ["ssid", "password", "auth", "hidden"], "email": ["to", "subject", "body"],
        "sms": ["number", "message"], "tel": ["number"], "geo": ["latitude", "longitude"],
        "vcard": ["firstName", "lastName", "organization", "title", "phone", "email", "url",
                  "address", "note"],
        "event": ["summary", "start", "end", "location", "description"],
    }
    for case in payload_cases():
        argv = []
        for key in order[case["type"]]:
            v = case["args"].get(key)
            if isinstance(v, bool):
                argv.append("1" if v else "0")
            elif v is None:
                argv.append("-")
            else:
                argv.append(repr(v) if isinstance(v, float) else str(v))
        code, out, err = run([PAYLOAD_CLI, case["type"]] + argv)
        assert code == 0, err
        payloads.append(dict(case, output=out))

    for name, data in (("render", renders), ("encode", encodes), ("payloads", payloads)):
        path = os.path.join(OUT, name + ".json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
            f.write("\n")
        print(f"{path}: {len(data)} cases, {os.path.getsize(path) // 1024} KiB")


if __name__ == "__main__":
    main()
