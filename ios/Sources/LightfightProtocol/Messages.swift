import Foundation

public enum ProtocolError: Error, Equatable {
    case length, version, range, crc
}

public let protocolVersion: UInt8 = 1

public struct Config: Equatable {
    public static let length = 23
    public var player: UInt8, team: UInt8, damageCode: UInt8
    public var friendlyFire: Bool
    public var maxHealth: UInt16, magazine: UInt16
    public var reloadMs: UInt16, respawnMs: UInt16, fireIntervalMs: UInt16
    public var damageTable: [UInt8]

    public init(player: UInt8, team: UInt8, damageCode: UInt8, friendlyFire: Bool,
                maxHealth: UInt16, magazine: UInt16, reloadMs: UInt16, respawnMs: UInt16,
                fireIntervalMs: UInt16, damageTable: [UInt8]) {
        self.player = player; self.team = team; self.damageCode = damageCode
        self.friendlyFire = friendlyFire; self.maxHealth = maxHealth; self.magazine = magazine
        self.reloadMs = reloadMs; self.respawnMs = respawnMs
        self.fireIntervalMs = fireIntervalMs; self.damageTable = damageTable
    }

    public func encode() throws -> Data {
        guard player <= 63, team <= 7, damageCode <= 7, damageTable.count == 8 else {
            throw ProtocolError.range
        }
        var w = Writer()
        w.u8(protocolVersion); w.u8(player); w.u8(team); w.u8(damageCode)
        w.u8(friendlyFire ? 1 : 0)
        w.u16(maxHealth); w.u16(magazine); w.u16(reloadMs); w.u16(respawnMs)
        w.u16(fireIntervalMs)
        damageTable.forEach { w.u8($0) }
        return w.data
    }

    public static func decode(_ d: Data) throws -> Config {
        var r = try Reader(d, length: length)
        let player = r.u8(), team = r.u8(), code = r.u8(), ff = r.u8()
        guard player <= 63, team <= 7, code <= 7, ff <= 1 else { throw ProtocolError.range }
        let mh = r.u16(), mg = r.u16(), rl = r.u16(), rs = r.u16(), fi = r.u16()
        let table = (0..<8).map { _ in r.u8() }
        return Config(player: player, team: team, damageCode: code, friendlyFire: ff == 1,
                      maxHealth: mh, magazine: mg, reloadMs: rl, respawnMs: rs,
                      fireIntervalMs: fi, damageTable: table)
    }
}

public struct State: Equatable {
    public static let length = 7
    public static let active: UInt8 = 1, alive: UInt8 = 2, reloading: UInt8 = 4
    public var flags: UInt8
    public var health: UInt16, ammo: UInt16
    public var batteryPct: UInt8

    public init(flags: UInt8, health: UInt16, ammo: UInt16, batteryPct: UInt8) {
        self.flags = flags; self.health = health; self.ammo = ammo; self.batteryPct = batteryPct
    }

    public func encode() throws -> Data {
        guard flags <= 7, batteryPct <= 100 else { throw ProtocolError.range }
        var w = Writer()
        w.u8(protocolVersion); w.u8(flags); w.u16(health); w.u16(ammo); w.u8(batteryPct)
        return w.data
    }

    public static func decode(_ d: Data) throws -> State {
        var r = try Reader(d, length: length)
        let flags = r.u8(), health = r.u16(), ammo = r.u16(), battery = r.u8()
        guard flags <= 7, battery <= 100 else { throw ProtocolError.range }
        return State(flags: flags, health: health, ammo: ammo, batteryPct: battery)
    }
}

public struct Event: Equatable {
    public static let length = 13
    public static let hit: UInt8 = 1, down: UInt8 = 2
    public var kind: UInt8
    public var eventID: UInt16
    public var shooter: UInt8, seq: UInt8, damage: UInt8
    public var healthAfter: UInt16
    public var tMs: UInt32

    public init(kind: UInt8, eventID: UInt16, shooter: UInt8, seq: UInt8, damage: UInt8,
                healthAfter: UInt16, tMs: UInt32) {
        self.kind = kind; self.eventID = eventID; self.shooter = shooter; self.seq = seq
        self.damage = damage; self.healthAfter = healthAfter; self.tMs = tMs
    }

    public func encode() throws -> Data {
        guard kind == Event.hit || kind == Event.down, shooter <= 63, seq <= 15 else {
            throw ProtocolError.range
        }
        var w = Writer()
        w.u8(protocolVersion); w.u8(kind); w.u16(eventID); w.u8(shooter); w.u8(seq)
        w.u8(damage); w.u16(healthAfter); w.u32(tMs)
        return w.data
    }

    public static func decode(_ d: Data) throws -> Event {
        var r = try Reader(d, length: length)
        let kind = r.u8(), id = r.u16(), shooter = r.u8(), seq = r.u8(), dmg = r.u8()
        let health = r.u16(), t = r.u32()
        guard kind == hit || kind == down, shooter <= 63, seq <= 15 else {
            throw ProtocolError.range
        }
        return Event(kind: kind, eventID: id, shooter: shooter, seq: seq, damage: dmg,
                     healthAfter: health, tMs: t)
    }
}

public enum Ack {
    public static func encode(eventID: UInt16) -> Data {
        var w = Writer(); w.u8(protocolVersion); w.u16(eventID); return w.data
    }
    public static func decode(_ d: Data) throws -> UInt16 {
        var r = try Reader(d, length: 3); return r.u16()
    }
}

public enum Command: UInt8 {
    case start = 1, stop = 2, respawn = 3, ping = 4

    public func encode() -> Data { Data([protocolVersion, rawValue]) }

    public static func decode(_ d: Data) throws -> Command {
        var r = try Reader(d, length: 2)
        guard let c = Command(rawValue: r.u8()) else { throw ProtocolError.range }
        return c
    }
}

/// An IR frame (docs/ir-protocol.md); the phone needs it for rosters and tests.
public struct IRFrame: Equatable {
    public var shooter: UInt8, team: UInt8, damage: UInt8, seq: UInt8

    public init(shooter: UInt8, team: UInt8, damage: UInt8, seq: UInt8) {
        self.shooter = shooter; self.team = team; self.damage = damage; self.seq = seq
    }

    public static func crc8(_ bytes: [UInt8]) -> UInt8 {
        var crc: UInt8 = 0
        for b in bytes {
            crc ^= b
            for _ in 0..<8 { crc = crc & 0x80 != 0 ? (crc << 1) ^ 0x07 : crc << 1 }
        }
        return crc
    }

    private static func crc(of payload: UInt16) -> UInt8 {
        crc8([UInt8(payload >> 8), UInt8(payload & 0xFF)])
    }

    public func pack() throws -> UInt32 {
        guard shooter <= 63, team <= 7, damage <= 7, seq <= 15 else { throw ProtocolError.range }
        let p = UInt16(shooter) << 10 | UInt16(team) << 7 | UInt16(damage) << 4 | UInt16(seq)
        return UInt32(p) << 8 | UInt32(IRFrame.crc(of: p))
    }

    public static func unpack(_ word: UInt32) throws -> IRFrame {
        guard word >> 24 == 0 else { throw ProtocolError.range }
        let p = UInt16(word >> 8)
        guard crc(of: p) == UInt8(word & 0xFF) else { throw ProtocolError.crc }
        return IRFrame(shooter: UInt8(p >> 10), team: UInt8((p >> 7) & 7),
                       damage: UInt8((p >> 4) & 7), seq: UInt8(p & 15))
    }
}

// MARK: little-endian helpers

struct Writer {
    var data = Data()
    mutating func u8(_ v: UInt8) { data.append(v) }
    mutating func u16(_ v: UInt16) { data.append(UInt8(v & 0xFF)); data.append(UInt8(v >> 8)) }
    mutating func u32(_ v: UInt32) { u16(UInt16(v & 0xFFFF)); u16(UInt16(v >> 16)) }
}

struct Reader {
    let bytes: [UInt8]
    var pos = 1 // the version byte is checked up front

    init(_ d: Data, length: Int) throws {
        guard d.count == length else { throw ProtocolError.length }
        bytes = [UInt8](d)
        guard bytes[0] == protocolVersion else { throw ProtocolError.version }
    }
    mutating func u8() -> UInt8 { defer { pos += 1 }; return bytes[pos] }
    mutating func u16() -> UInt16 { UInt16(u8()) | UInt16(u8()) << 8 }
    mutating func u32() -> UInt32 { UInt32(u16()) | UInt32(u16()) << 16 }
}
