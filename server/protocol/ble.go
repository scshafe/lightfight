// Package protocol is the Go side of the lightfight wire formats: the BLE
// messages (docs/ble-protocol.md) and the IR frame word (docs/ir-protocol.md).
// The firmware's C library is the reference; protocol/*.csv are the shared
// vectors every implementation reproduces.
package protocol

import (
	"encoding/binary"
	"errors"
)

const Version = 1

const (
	ConfigLen  = 23
	StateLen   = 7
	EventLen   = 13
	AckLen     = 3
	CommandLen = 2
)

// State flags.
const (
	StateActive    = 1
	StateAlive     = 2
	StateReloading = 4
	StateDisabled  = 8
)

// Event kinds.
const (
	EventHit  = 1
	EventDown = 2
)

// Commands.
const (
	CmdStart   = 1
	CmdStop    = 2
	CmdRespawn = 3
	CmdPing    = 4
	CmdDisable = 5
	CmdEnable  = 6
)

var (
	ErrLength  = errors.New("protocol: wrong length")
	ErrVersion = errors.New("protocol: unsupported version")
	ErrRange   = errors.New("protocol: field out of range")
	// ErrBadCRC is returned for an IR word whose CRC does not match.
	ErrBadCRC = errors.New("protocol: bad CRC")
)

type Config struct {
	Player, Team, DamageCode uint8
	FriendlyFire             bool
	MaxHealth, Magazine      uint16
	ReloadMs, RespawnMs      uint16
	FireIntervalMs           uint16
	DamageTable              [8]uint8
}

type State struct {
	Flags      uint8
	Health     uint16
	Ammo       uint16
	BatteryPct uint8
}

type Event struct {
	Kind        uint8
	EventID     uint16
	Shooter     uint8
	Seq         uint8
	Damage      uint8
	HealthAfter uint16
	TMs         uint32
}

func check(b []byte, n int) error {
	if len(b) != n {
		return ErrLength
	}
	if b[0] != Version {
		return ErrVersion
	}
	return nil
}

func (c Config) Encode() ([]byte, error) {
	if c.Player > 63 || c.Team > 7 || c.DamageCode > 7 {
		return nil, ErrRange
	}
	b := make([]byte, ConfigLen)
	b[0], b[1], b[2], b[3] = Version, c.Player, c.Team, c.DamageCode
	if c.FriendlyFire {
		b[4] = 1
	}
	binary.LittleEndian.PutUint16(b[5:], c.MaxHealth)
	binary.LittleEndian.PutUint16(b[7:], c.Magazine)
	binary.LittleEndian.PutUint16(b[9:], c.ReloadMs)
	binary.LittleEndian.PutUint16(b[11:], c.RespawnMs)
	binary.LittleEndian.PutUint16(b[13:], c.FireIntervalMs)
	copy(b[15:], c.DamageTable[:])
	return b, nil
}

func DecodeConfig(b []byte) (Config, error) {
	var c Config
	if err := check(b, ConfigLen); err != nil {
		return c, err
	}
	if b[1] > 63 || b[2] > 7 || b[3] > 7 || b[4] > 1 {
		return c, ErrRange
	}
	c.Player, c.Team, c.DamageCode, c.FriendlyFire = b[1], b[2], b[3], b[4] == 1
	c.MaxHealth = binary.LittleEndian.Uint16(b[5:])
	c.Magazine = binary.LittleEndian.Uint16(b[7:])
	c.ReloadMs = binary.LittleEndian.Uint16(b[9:])
	c.RespawnMs = binary.LittleEndian.Uint16(b[11:])
	c.FireIntervalMs = binary.LittleEndian.Uint16(b[13:])
	copy(c.DamageTable[:], b[15:])
	return c, nil
}

func (s State) Encode() ([]byte, error) {
	if s.Flags > 15 || s.BatteryPct > 100 {
		return nil, ErrRange
	}
	b := make([]byte, StateLen)
	b[0], b[1] = Version, s.Flags
	binary.LittleEndian.PutUint16(b[2:], s.Health)
	binary.LittleEndian.PutUint16(b[4:], s.Ammo)
	b[6] = s.BatteryPct
	return b, nil
}

func DecodeState(b []byte) (State, error) {
	var s State
	if err := check(b, StateLen); err != nil {
		return s, err
	}
	if b[1] > 15 || b[6] > 100 {
		return s, ErrRange
	}
	s.Flags, s.BatteryPct = b[1], b[6]
	s.Health = binary.LittleEndian.Uint16(b[2:])
	s.Ammo = binary.LittleEndian.Uint16(b[4:])
	return s, nil
}

func (e Event) Encode() ([]byte, error) {
	if (e.Kind != EventHit && e.Kind != EventDown) || e.Shooter > 63 || e.Seq > 15 {
		return nil, ErrRange
	}
	b := make([]byte, EventLen)
	b[0], b[1] = Version, e.Kind
	binary.LittleEndian.PutUint16(b[2:], e.EventID)
	b[4], b[5], b[6] = e.Shooter, e.Seq, e.Damage
	binary.LittleEndian.PutUint16(b[7:], e.HealthAfter)
	binary.LittleEndian.PutUint32(b[9:], e.TMs)
	return b, nil
}

func DecodeEvent(b []byte) (Event, error) {
	var e Event
	if err := check(b, EventLen); err != nil {
		return e, err
	}
	if (b[1] != EventHit && b[1] != EventDown) || b[4] > 63 || b[5] > 15 {
		return e, ErrRange
	}
	e.Kind = b[1]
	e.EventID = binary.LittleEndian.Uint16(b[2:])
	e.Shooter, e.Seq, e.Damage = b[4], b[5], b[6]
	e.HealthAfter = binary.LittleEndian.Uint16(b[7:])
	e.TMs = binary.LittleEndian.Uint32(b[9:])
	return e, nil
}

func EncodeAck(eventID uint16) []byte {
	b := make([]byte, AckLen)
	b[0] = Version
	binary.LittleEndian.PutUint16(b[1:], eventID)
	return b
}

func DecodeAck(b []byte) (uint16, error) {
	if err := check(b, AckLen); err != nil {
		return 0, err
	}
	return binary.LittleEndian.Uint16(b[1:]), nil
}

func EncodeCommand(cmd uint8) ([]byte, error) {
	if cmd < CmdStart || cmd > CmdEnable {
		return nil, ErrRange
	}
	return []byte{Version, cmd}, nil
}

func DecodeCommand(b []byte) (uint8, error) {
	if err := check(b, CommandLen); err != nil {
		return 0, err
	}
	if b[1] < CmdStart || b[1] > CmdEnable {
		return 0, ErrRange
	}
	return b[1], nil
}
