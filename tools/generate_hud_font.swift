// Rasterize the HUD subset from the upstream Orbitron Bold TTF (SIL OFL).
// swift -module-cache-path build/swift-module-cache tools/generate_hud_font.swift \
//   /path/to/Orbitron-Bold.ttf preview/hud_font.js > firmware/AttitudeIndicator/hud_font.h
// Upstream: https://github.com/theleagueof/orbitron/blob/master/Orbitron%20Bold.ttf
import Foundation
import CoreText
import CoreGraphics

guard CommandLine.arguments.count == 3 else {
    fatalError("Expected the Orbitron Bold TTF and the output path for the browser atlas")
}
let width = 32, height = 32, size: CGFloat = 32, baseline = 26, originX = 6
// Fit the existing four-digit window while retaining the original cap height.
let advance: CGFloat = 19.203125
let horizontalScale: CGFloat = 0.76
let characters = "0123456789NSEWKM/HV+-. IU"
let sourceURL = URL(fileURLWithPath: CommandLine.arguments[1])
guard let provider = CGDataProvider(url: sourceURL as CFURL),
      let graphicsFont = CGFont(provider) else {
    fatalError("Cannot read TTF at \(sourceURL.path)")
}
let font = CTFontCreateWithGraphicsFont(graphicsFont, size, nil, nil)
let postScriptName = CTFontCopyPostScriptName(font) as String
precondition(postScriptName == "Orbitron-Bold", "Expected Orbitron-Bold, got \(postScriptName)")
let digitCenter = Double(CGFloat(baseline) - CTFontGetCapHeight(font) / 2)

var glyphs = [[UInt8]]()
var bounds = [[Int]]()
for scalar in characters.unicodeScalars {
    var code = UniChar(scalar.value), glyph = CGGlyph()
    precondition(CTFontGetGlyphsForCharacters(font, &code, &glyph, 1), "Missing glyph \(scalar)")
    let ink = CTFontGetBoundingRectsForGlyphs(font, .horizontal, &glyph, nil, 1)
    var pixels = [UInt8](repeating: 0, count: width * height)
    if scalar != " " {
        pixels.withUnsafeMutableBytes { bytes in
            guard let context = CGContext(data: bytes.baseAddress, width: width, height: height,
                bitsPerComponent: 8, bytesPerRow: width, space: CGColorSpaceCreateDeviceGray(),
                bitmapInfo: CGImageAlphaInfo.none.rawValue) else {
                fatalError("Cannot create glyph bitmap")
            }
            context.setAllowsAntialiasing(true)
            context.setShouldAntialias(true)
            context.setAllowsFontSmoothing(false)
            context.setShouldSmoothFonts(false)
            context.setFillColor(gray: 1, alpha: 1)
            // Center the ink in its cell so proportional digits never drift during carries.
            context.translateBy(x: CGFloat(originX) + (advance - ink.width * horizontalScale) / 2,
                                y: CGFloat(height - baseline))
            context.scaleBy(x: horizontalScale, y: 1)
            var position = CGPoint(x: -ink.minX, y: 0)
            CTFontDrawGlyphs(font, &glyph, &position, 1, context)
        }
        let covered = pixels.indices.filter { pixels[$0] != 0 }
        precondition(!covered.isEmpty, "Empty glyph \(scalar)")
        let xs = covered.map { $0 % width }, ys = covered.map { $0 / width }
        let l = xs.min()!, t = ys.min()!, r = xs.max()!, b = ys.max()!
        precondition(l > 0 && r < width - 1 && t > 0 && b < height - 1,
                     "Glyph \(scalar) touches atlas boundary")
        if scalar.value >= 48 && scalar.value <= 57 {
            precondition(l >= originX && CGFloat(r + 1) <= CGFloat(originX) + advance,
                         "Digit \(scalar) exceeds its readout column")
        }
        bounds.append([l, t, r, b])
    } else {
        bounds.append([width, height, -1, -1])
    }
    glyphs.append(pixels)
}

let preview: [String: Any] = [
    "width": width, "height": height, "size": Int(size), "originX": originX,
    "advance": Double(advance), "digitCenterY": digitCenter,
    "chars": characters, "bounds": bounds,
    "coverage": Data(glyphs.flatMap { $0 }).base64EncodedString()
]
let json = try JSONSerialization.data(withJSONObject: preview, options: [.sortedKeys, .withoutEscapingSlashes])
let js = "// Generated Orbitron Bold HUD glyphs. See HUD_FONT_LICENSE.txt.\nconst HUD_FONT = "
    + String(decoding: json, as: UTF8.self) + ";\n"
try js.write(toFile: CommandLine.arguments[2], atomically: true, encoding: .utf8)

print("""
// Generated HUD glyph atlas from Orbitron Bold (SIL OFL 1.1).
// See HUD_FONT_LICENSE.txt. Regenerate with tools/generate_hud_font.swift.
#pragma once
#include <cstdint>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_attr.h>
#define HUD_FONT_DATA DRAM_ATTR
#else
#define HUD_FONT_DATA
#endif
namespace instrument { namespace hudfont {
static constexpr int Width = \(width);
static constexpr int Height = \(height);
static constexpr int Size = \(Int(size));
static constexpr int OriginX = \(originX);
static constexpr float Advance = \(advance)f;
static constexpr float DigitCenterY = \(digitCenter)f;
static constexpr char chars[] = "\(characters)";
static constexpr int CharCount = sizeof(chars) - 1;
// Alpha coverage, shared by the HUD and round attitude renderer.
inline auto glyphs() -> const uint8_t (&)[CharCount][Width * Height] {
  static const uint8_t HUD_FONT_DATA data[CharCount][Width * Height] = {
""")
for (index, pixels) in glyphs.enumerated() {
    let code = Array(characters.unicodeScalars)[index].value
    print("{ // U+\(String(code, radix: 16, uppercase: true))")
    for row in 0..<height {
        print(pixels[row * width..<(row + 1) * width].map(String.init).joined(separator: ",") + ",")
    }
    print("},")
}
print("  };\n  return data;\n}")
print("// Inclusive ink bounds. A negative right edge marks a blank glyph.")
print("struct InkBounds { int8_t l, t, r, b; };")
print("static constexpr InkBounds inkBounds[CharCount] = {")
for box in bounds {
    print("{\(box.map(String.init).joined(separator: ","))},")
}
print("};\n} } // namespace instrument::hudfont")
