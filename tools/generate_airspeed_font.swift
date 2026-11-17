// Regenerate the vector glyph subset from the same Orbitron Bold TTF as the HUD.
// swift -module-cache-path build/swift-module-cache tools/generate_airspeed_font.swift \
//   /path/to/Orbitron-Bold.ttf preview/dial_font.js > firmware/AttitudeIndicator/airspeed_font.h
import Foundation
import CoreText
import CoreGraphics

guard (2...3).contains(CommandLine.arguments.count),
      let provider = CGDataProvider(url: URL(fileURLWithPath: CommandLine.arguments[1]) as CFURL),
      let graphicsFont = CGFont(provider) else { fatalError("Expected a font file") }
let font = CTFontCreateWithGraphicsFont(graphicsFont, 1000, nil, nil)
precondition(CTFontCopyPostScriptName(font) as String == "Orbitron-Bold")
// Match the horizontal fit used by the HUD and preserve vertical cap height.
let horizontalScale: CGFloat = 0.76
var allEdges: [(Int,Int,Int,Int)] = []
var glyphs: [(UInt32,Int,Int,Int)] = []
for scalar in "0123456789AIR SPEEDALT METERSkm/h".unicodeScalars.sorted(by: {$0.value < $1.value}) {
    if glyphs.contains(where: {$0.0 == scalar.value}) { continue }
    var code = UniChar(scalar.value), glyph = CGGlyph()
    guard CTFontGetGlyphsForCharacters(font, &code, &glyph, 1) else { fatalError("Missing glyph") }
    var advance = CGSize.zero
    CTFontGetAdvancesForGlyphs(font, .horizontal, &glyph, &advance, 1)
    let start = allEdges.count
    var current = CGPoint.zero, first = CGPoint.zero
    func edge(_ p: CGPoint) {
        if p != current {
            allEdges.append((Int((current.x * horizontalScale).rounded()), Int(current.y.rounded()),
                             Int((p.x * horizontalScale).rounded()), Int(p.y.rounded())))
        }
        current = p
    }
    CTFontCreatePathForGlyph(font, glyph, nil)?.applyWithBlock { elementPointer in
        let e = elementPointer.pointee
        switch e.type {
        case .moveToPoint: current=e.points[0]; first=current
        case .addLineToPoint: edge(e.points[0])
        case .addQuadCurveToPoint:
            let a=current,b=e.points[0],c=e.points[1]
            for i in 1...12 { let t=CGFloat(i)/12,u=1-t; edge(CGPoint(x:u*u*a.x+2*u*t*b.x+t*t*c.x,y:u*u*a.y+2*u*t*b.y+t*t*c.y)) }
        case .addCurveToPoint:
            let a=current,b=e.points[0],c=e.points[1],d=e.points[2]
            for i in 1...16 { let t=CGFloat(i)/16,u=1-t; edge(CGPoint(x:u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*c.x+t*t*t*d.x,y:u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*c.y+t*t*t*d.y)) }
        case .closeSubpath: edge(first)
        @unknown default: fatalError("Unsupported font path")
        }
    }
    glyphs.append((scalar.value,start,allEdges.count-start,Int((advance.width * horizontalScale).rounded())))
}
print("// Generated vector outlines from Orbitron Bold. See HUD_FONT_LICENSE.txt.\n#pragma once\n#include <cstdint>\nnamespace instrument { namespace airfont {\nstruct Edge { int16_t x1,y1,x2,y2; };\nstruct Glyph { char code; uint16_t first,count,advance; };\nstatic const Edge edges[]={")
for e in allEdges { print("{\(e.0),\(e.1),\(e.2),\(e.3)},") }
print("};\nstatic const Glyph glyphs[]={")
for g in glyphs { print("{\(g.0),\(g.1),\(g.2),\(g.3)},") }
print("};\n} }")

if CommandLine.arguments.count == 3 {
    let data = try JSONSerialization.data(withJSONObject: [
        "edges": allEdges.map { [$0.0, $0.1, $0.2, $0.3] },
        "glyphs": glyphs.map { [Int($0.0), $0.1, $0.2, $0.3] }
    ], options: [.sortedKeys])
    let js = "// Generated Orbitron Bold dial outlines. See HUD_FONT_LICENSE.txt.\nconst DIAL_FONT = "
        + String(decoding: data, as: UTF8.self) + ";\n"
    try js.write(toFile: CommandLine.arguments[2], atomically: true, encoding: .utf8)
}
