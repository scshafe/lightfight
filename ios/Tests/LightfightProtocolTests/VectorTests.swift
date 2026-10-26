import Foundation
import XCTest
@testable import LightfightProtocol

final class VectorTests: XCTestCase {
    /// Data lines of a shared vector file (../protocol/ at the repository root).
    private func vectors(_ name: String, file: StaticString = #filePath) throws -> [[String]] {
        let root = URL(fileURLWithPath: "\(file)")
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        let text = try String(contentsOf: root.appendingPathComponent("protocol/\(name)"))
        return text.split(separator: "\n")
            .filter { !$0.hasPrefix("#") && !$0.isEmpty }
            .map { $0.split(separator: ",", omittingEmptySubsequences: false).map(String.init) }
    }

    private func bytes(_ hex: String) -> Data {
        var d = Data()
        var i = hex.startIndex
        while i < hex.endIndex {
            let j = hex.index(i, offsetBy: 2)
            d.append(UInt8(hex[i..<j], radix: 16)!)
            i = j
        }
        return d
    }

    func testIRVectors() throws {
        let rows = try vectors("ir-vectors.csv")
        XCTAssertGreaterThanOrEqual(rows.count, 5)
        for r in rows {
            let f = IRFrame(shooter: UInt8(r[0])!, team: UInt8(r[1])!, damage: UInt8(r[2])!,
                            seq: UInt8(r[3])!)
            let word = UInt32(r[4], radix: 16)!
            XCTAssertEqual(try f.pack(), word)
            XCTAssertEqual(try IRFrame.unpack(word), f)
        }
        XCTAssertThrowsError(try IRFrame.unpack(0x1539B9 ^ 1)) {
            XCTAssertEqual($0 as? ProtocolError, .crc)
        }
    }

    func testBLEVectors() throws {
        let rows = try vectors("ble-vectors.csv")
        XCTAssertGreaterThanOrEqual(rows.count, 14)
        for r in rows {
            let hex = bytes(r.last!)
            let v = r.dropFirst().dropLast().map { UInt64($0)! }
            switch r[0] {
            case "config":
                let c = Config(player: UInt8(v[0]), team: UInt8(v[1]), damageCode: UInt8(v[2]),
                               friendlyFire: v[8] != 0, maxHealth: UInt16(v[3]),
                               magazine: UInt16(v[4]), reloadMs: UInt16(v[5]),
                               respawnMs: UInt16(v[6]), fireIntervalMs: UInt16(v[7]),
                               damageTable: v[9...].map { UInt8($0) })
                XCTAssertEqual(try c.encode(), hex)
                XCTAssertEqual(try Config.decode(hex), c)
            case "state":
                let s = State(flags: UInt8(v[0]), health: UInt16(v[1]), ammo: UInt16(v[2]),
                              batteryPct: UInt8(v[3]))
                XCTAssertEqual(try s.encode(), hex)
                XCTAssertEqual(try State.decode(hex), s)
            case "event":
                let e = Event(kind: UInt8(v[0]), eventID: UInt16(v[1]), shooter: UInt8(v[2]),
                              seq: UInt8(v[3]), damage: UInt8(v[4]), healthAfter: UInt16(v[5]),
                              tMs: UInt32(v[6]))
                XCTAssertEqual(try e.encode(), hex)
                XCTAssertEqual(try Event.decode(hex), e)
            case "ack":
                XCTAssertEqual(Ack.encode(eventID: UInt16(v[0])), hex)
                XCTAssertEqual(try Ack.decode(hex), UInt16(v[0]))
            case "command":
                let c = Command(rawValue: UInt8(v[0]))!
                XCTAssertEqual(c.encode(), hex)
                XCTAssertEqual(try Command.decode(hex), c)
            default:
                XCTFail("unknown kind \(r[0])")
            }
        }
    }

    func testRejects() {
        XCTAssertThrowsError(try Config.decode(Data(count: 22))) {
            XCTAssertEqual($0 as? ProtocolError, .length)
        }
        var bad = Data(count: 23); bad[0] = 2
        XCTAssertThrowsError(try Config.decode(bad)) {
            XCTAssertEqual($0 as? ProtocolError, .version)
        }
        bad[0] = 1; bad[1] = 64
        XCTAssertThrowsError(try Config.decode(bad)) {
            XCTAssertEqual($0 as? ProtocolError, .range)
        }
        XCTAssertThrowsError(try State.decode(Data([1, 8, 0, 0, 0, 0, 0])))
        XCTAssertThrowsError(try Event.decode(Data([1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])))
        XCTAssertThrowsError(try Command.decode(Data([1, 5])))
    }
}
