#!/usr/bin/env python3
"""Generate small, deterministic .kpp fixtures for the resource tests."""
from __future__ import annotations

import gzip
import pathlib
import struct
import zlib

HERE = pathlib.Path(__file__).resolve().parent
PRESETS = HERE / "presets"
OVERRIDE = HERE / "override"


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def png_with_metadata_chunk(kind: bytes, text: bytes) -> bytes:
    width = height = 16
    raw = b"".join(b"\x00" + b"\x00\x00\x00\x00" * width for _ in range(height))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"".join(
        [
            b"\x89PNG\r\n\x1a\n",
            png_chunk(b"IHDR", ihdr),
            png_chunk(kind, text),
            png_chunk(b"tEXt", b"version\x002.2"),
            png_chunk(b"IDAT", zlib.compress(raw, 9)),
            png_chunk(b"IEND", b""),
        ]
    )


def png_with_preset(xml: str) -> bytes:
    text = b"preset\x00\x00" + zlib.compress(xml.encode("utf-8"), 9)
    return png_with_metadata_chunk(b"zTXt", text)


def png_with_text_preset(xml: str) -> bytes:
    return png_with_metadata_chunk(b"tEXt", b"preset\x00" + xml.encode("utf-8"))


def png_with_itxt_preset(xml: str) -> bytes:
    # keyword, compression flag 1, method 0, empty language/translation, data.
    text = (
        b"preset\x00"
        + bytes([1, 0])
        + b"\x00"
        + b"\x00"
        + zlib.compress(xml.encode("utf-8"), 9)
    )
    return png_with_metadata_chunk(b"iTXt", text)


def preset_xml(
    name: str,
    paintop: str,
    opacity: str,
    flow: str,
    diameter: int,
    hfade: str,
    vfade: str,
    composite: str = "normal",
) -> str:
    return f"""<Preset name="{name}" paintopid="{paintop}">
 <param name="CompositeOp" type="string"><![CDATA[{composite}]]></param>
 <param name="FlowValue" type="string"><![CDATA[{flow}]]></param>
 <param name="OpacityValue" type="string"><![CDATA[{opacity}]]></param>
 <param name="PressureSize" type="string"><![CDATA[true]]></param>
 <param name="Unknown/FutureField" type="string"><![CDATA[kept]]></param>
 <param name="brush_definition" type="string"><![CDATA[<Brush type="auto_brush" spacing="0.2" useAutoSpacing="1" autoSpacingCoeff="0.8"><MaskGenerator type="circle" diameter="{diameter}" hfade="{hfade}" vfade="{vfade}" ratio="1" spikes="2" antialiasEdges="1"/></Brush>]]></param>
</Preset>
"""


def main() -> None:
    PRESETS.mkdir(parents=True, exist_ok=True)
    (OVERRIDE / "system").mkdir(parents=True, exist_ok=True)
    (OVERRIDE / "user").mkdir(parents=True, exist_ok=True)

    basic = preset_xml("b)_Basic-5_Size", "paintbrush", "0.8", "0.5", 42, "1", "0.5")
    (PRESETS / "basic.kpp").write_bytes(png_with_preset(basic))

    eraser = preset_xml(
        "a)_Eraser_Circle", "paintbrush", "1", "1", 20, "0.87", "0.87", composite="erase"
    )
    (PRESETS / "eraser.kpp").write_bytes(png_with_preset(eraser))

    compressed = preset_xml(
        "Compressed_Test", "paintbrush", "1", "0.75", 16, "1", "1"
    )
    (PRESETS / "compressed.kpp").write_bytes(gzip.compress(compressed.encode("utf-8"), 9))

    plain = preset_xml("Plain_Test", "paintbrush", "1", "1", 12, "1", "1")
    (PRESETS / "plain.kpp").write_text(plain, encoding="utf-8")

    text_chunk = preset_xml("Text_Chunk_Test", "paintbrush", "1", "1", 14, "1", "1")
    (PRESETS / "text.kpp").write_bytes(png_with_text_preset(text_chunk))

    itxt = preset_xml("ITXt_Test", "paintbrush", "1", "1", 18, "1", "1")
    (PRESETS / "itxt.kpp").write_bytes(png_with_itxt_preset(itxt))

    (PRESETS / "broken.kpp").write_text("This is not a Krita preset.\n", encoding="utf-8")

    system = preset_xml("System_Preset", "paintbrush", "1", "1", 10, "1", "1")
    user = preset_xml("User_Preset", "paintbrush", "1", "1", 30, "1", "1")
    (OVERRIDE / "system" / "override.kpp").write_bytes(png_with_preset(system))
    (OVERRIDE / "user" / "override.kpp").write_bytes(png_with_preset(user))


if __name__ == "__main__":
    main()
