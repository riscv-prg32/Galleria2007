// Headless runner for the unmodified PRG32-iOS core (PRG32Core package):
// ios-headless file.prg32 frames out.ppm [frame:mask ...]
import Foundation
import PRG32Core

let args = CommandLine.arguments
let data = try Data(contentsOf: URL(fileURLWithPath: args[1]))
let frames = Int(args[2]) ?? 300
var script: [Int: UInt32] = [:]
for spec in args.dropFirst(4) {
    let p = spec.split(separator: ":")
    script[Int(p[0])!] = UInt32(p[1])!
}
do {
    let cart = try PRG32Cartridge.parse(data)
    let rt = try PRG32Runtime(cartridge: cart)
    try rt.initialize()
    var hashes = Set<Int>()
    for f in 0..<frames {
        if let m = script[f] { rt.input = m }
        try rt.frame()
        if f % 15 == 0 { hashes.insert(rt.framebuffer.rgb565Pixels().hashValue) }
        usleep(33_000)   // the cartridge's fixed 33 ms logic tick runs on wall time
    }
    let px = rt.framebuffer.rgb565Pixels()
    var out = Data("P6\n320 200\n255\n".utf8)
    for v in px {
        out.append(UInt8(((v >> 11) & 31) * 255 / 31)); out.append(UInt8(((v >> 5) & 63) * 255 / 63)); out.append(UInt8((v & 31) * 255 / 31))
    }
    try out.write(to: URL(fileURLWithPath: args[3]))
    print("\(args[1].split(separator: "/").last!): OK on PRG32-iOS core (\(frames) frames, \(hashes.count) distinct frames, \(Set(px).count) colours)")
} catch {
    print("\(args[1].split(separator: "/").last!): REJECTED/FAILED on PRG32-iOS core: \(error)")
    exit(1)
}
