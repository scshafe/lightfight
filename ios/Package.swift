// swift-tools-version:5.9
import PackageDescription

// The wire formats (docs/ble-protocol.md, docs/ir-protocol.md) as a library with
// no UI and no CoreBluetooth, so its tests run anywhere Swift does:
//   swift test
let package = Package(
    name: "Lightfight",
    products: [.library(name: "LightfightProtocol", targets: ["LightfightProtocol"])],
    targets: [
        .target(name: "LightfightProtocol"),
        .testTarget(
            name: "LightfightProtocolTests",
            dependencies: ["LightfightProtocol"]
        ),
    ]
)
