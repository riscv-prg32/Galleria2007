// swift-tools-version: 6.0
// Headless runner for the unmodified PRG32-iOS emulator core (PRG32Core).
// PRG32_IOS_REPO points to a PRG32-iOS checkout (default ../../../PRG32-iOS,
// i.e. a sibling of this repository).
import Foundation
import PackageDescription

let iosRepo = ProcessInfo.processInfo.environment["PRG32_IOS_REPO"] ?? "../../../PRG32-iOS"
let package = Package(
    name: "ios-headless",
    platforms: [.macOS(.v14)],
    dependencies: [.package(path: iosRepo)],
    targets: [.executableTarget(name: "ios-headless",
                                dependencies: [.product(name: "PRG32Core", package: "PRG32-iOS")])]
)
