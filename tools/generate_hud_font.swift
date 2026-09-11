// Generate the Instrument HUD Bitmap subset from Liberation Mono 2.1.5 (SIL OFL).
// swift -module-cache-path build/swift-module-cache tools/generate_hud_font.swift \
//   /path/to/LiberationMono-Regular.ttf /path/to/LiberationMono-Bold.ttf \
//   /private/tmp/hud-font-atlas.png > firmware/AttitudeIndicator/hud_font.h
// The optional PNG contains regular and bold rows, enlarged 3x for inspection.
import Foundation
import CoreText
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

guard (3...4).contains(CommandLine.arguments.count) else {
    fatalError("Expected explicit regular and bold TTF paths, and optional atlas PNG path")
}
let width = 24, height = 40, size: CGFloat = 32, baseline = 30, originX = 2
var seen = Set<UInt32>()
let characters = Array("0123456789NSEWKM/HV+-. IMU".unicodeScalars.filter { seen.insert($0.value).inserted })
var allPixels = [[[UInt8]]]()
var measuredAdvance: CGFloat = 0
var digitCenters = [Double]()

for (weight, path) in CommandLine.arguments[1...2].enumerated() {
    guard let provider = CGDataProvider(url: URL(fileURLWithPath: path) as CFURL),
          let graphicsFont = CGFont(provider) else { fatalError("Cannot read TTF: \(path)") }
    let font = CTFontCreateWithGraphicsFont(graphicsFont, size, nil, nil)
    let expected = weight == 0 ? "LiberationMono" : "LiberationMono-Bold"
    guard CTFontCopyPostScriptName(font) as String == expected else {
        fatalError("Expected \(expected), got \(CTFontCopyPostScriptName(font))")
    }
    var fontPixels = [[UInt8]]()
    for scalar in characters {
        var code = UniChar(scalar.value), glyph = CGGlyph()
        guard CTFontGetGlyphsForCharacters(font, &code, &glyph, 1) else {
            fatalError("Missing glyph \(scalar)")
        }
        var advance = CGSize.zero
        CTFontGetAdvancesForGlyphs(font, .horizontal, &glyph, &advance, 1)
        if measuredAdvance == 0 { measuredAdvance = advance.width }
        precondition(abs(advance.width - measuredAdvance) < 0.001, "Font must be monospaced")
        var pixels = [UInt8](repeating: 0, count: width * height)
        pixels.withUnsafeMutableBytes { bytes in
            guard let context = CGContext(data: bytes.baseAddress, width: width, height: height,
                bitsPerComponent: 8, bytesPerRow: width, space: CGColorSpaceCreateDeviceGray(),
                bitmapInfo: CGImageAlphaInfo.none.rawValue) else { fatalError("Cannot create bitmap") }
            context.setAllowsAntialiasing(true)
            context.setShouldAntialias(true)
            context.setAllowsFontSmoothing(false)
            context.setShouldSmoothFonts(false)
            context.setFillColor(gray: 1, alpha: 1)
            var position = CGPoint(x: originX, y: height - baseline)
            CTFontDrawGlyphs(font, &glyph, &position, 1, context)
        }
        // Quartz draws in upwards coordinates, but bitmap memory begins at the top row.
        let topDown = pixels
        let ink = topDown.indices.filter { topDown[$0] > 0 }
        if !ink.isEmpty {
            precondition(ink.allSatisfy { let x = $0 % width, y = $0 / width
                return x > 0 && x < width - 1 && y > 0 && y < height - 1
            }, "Glyph \(scalar) touches atlas boundary")
            if scalar.value >= 48 && scalar.value <= 57 {
                let rows = ink.map { $0 / width }
                digitCenters.append(Double(rows.min()! + rows.max()! + 1) / 2)
            }
        } else { precondition(scalar == " ", "Unexpected empty glyph") }
        fontPixels.append(topDown)
    }
    allPixels.append(fontPixels)
}

let digitCenter = digitCenters.reduce(0, +) / Double(digitCenters.count)
print("""
// Generated Instrument HUD Bitmap font: Liberation Mono Regular/Bold 2.1.5.
// Font bitmap data is SIL OFL 1.1; see HUD_FONT_LICENSE.txt. Do not edit by hand.
// Regenerate with tools/generate_hud_font.swift and the two explicit TTF paths.
#pragma once
#include <cstdint>
namespace instrument { namespace hudfont {
static constexpr int Width = \(width);
static constexpr int Height = \(height);
static constexpr int Size = \(Int(size));
static constexpr int Baseline = \(baseline);
static constexpr int OriginX = \(originX);
static constexpr float Advance = \(measuredAdvance)f;
// Center in top-down pixel-edge coordinates; pixel centers are at y + 0.5.
static constexpr float DigitCenterY = \(digitCenter)f;
static constexpr int Regular = 0;
static constexpr int Bold = 1;
static constexpr char chars[] = "\(String(String.UnicodeScalarView(characters)))";
static constexpr int CharCount = sizeof(chars) - 1;
// Alpha coverage, row-major from the top left, 0 transparent to 255 opaque.
static const uint8_t glyphs[2][CharCount][Width * Height] = {
""")
for (weight, fontPixels) in allPixels.enumerated() {
    print("{ // \(weight == 0 ? "Regular" : "Bold")")
    for (index, pixels) in fontPixels.enumerated() {
        print("{ // U+\(String(characters[index].value, radix: 16, uppercase: true))")
        for row in 0..<height {
            print(pixels[row * width..<(row + 1) * width].map(String.init).joined(separator: ",") + ",")
        }
        print("},")
    }
    print("},")
}
print("};")
print("// Union of regular/bold ink, inclusive atlas coordinates; -1 marks a blank glyph.")
print("struct InkBounds { int8_t l, t, r, b; };")
print("static constexpr InkBounds inkBounds[CharCount] = {")
for character in 0..<characters.count {
    var left = width, top = height, right = -1, bottom = -1
    for weight in 0..<2 { for y in 0..<height { for x in 0..<width {
        if allPixels[weight][character][y * width + x] != 0 {
            left = min(left, x); right = max(right, x)
            top = min(top, y); bottom = max(bottom, y)
        }
    } } }
    print("{\(left),\(top),\(right),\(bottom)},")
}
print("};\n} } // namespace instrument::hudfont")

if CommandLine.arguments.count == 4 {
    let scale = 3, atlasWidth = width * characters.count, atlasHeight = height * 2
    var atlas = [UInt8](repeating: 0, count: atlasWidth * atlasHeight * scale * scale)
    for weight in 0..<2 { for character in 0..<characters.count {
        for y in 0..<height { for x in 0..<width {
            for sy in 0..<scale { for sx in 0..<scale {
                let ax = (character * width + x) * scale + sx
                let ay = (weight * height + y) * scale + sy
                atlas[ay * atlasWidth * scale + ax] = allPixels[weight][character][y * width + x]
            } }
        } }
    } }
    let data = Data(atlas)
    guard let provider = CGDataProvider(data: data as CFData),
          let image = CGImage(width: atlasWidth * scale, height: atlasHeight * scale,
            bitsPerComponent: 8, bitsPerPixel: 8, bytesPerRow: atlasWidth * scale,
            space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGBitmapInfo(rawValue: 0),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent),
          let destination = CGImageDestinationCreateWithURL(
            URL(fileURLWithPath: CommandLine.arguments[3]) as CFURL, UTType.png.identifier as CFString, 1, nil)
    else { fatalError("Cannot create atlas PNG") }
    CGImageDestinationAddImage(destination, image, nil)
    precondition(CGImageDestinationFinalize(destination), "Cannot write atlas PNG")
}
