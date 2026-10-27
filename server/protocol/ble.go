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
	ConfigLen  = 40
	StateLen   = 12
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

// Modes is the number of firing modes: light, heavy, shotgun.
const Modes = 3

// Firing modes.
const (
	ModeLight   = 0
	ModeHeavy   = 1
	ModeShotgun = 2
)

// Mode is one firing mode's settings.
type Mode struct {
	DamageCode     uint8
	AutoReload     bool
	Magazine       uint16
	ReloadMs       uint16
	FireIntervalMs uint16
}

type Config struct {
	Player, Team uint8
	FriendlyFire bool
	MaxHealth    uint16
	RespawnMs    uint16
	DamageTable  [8]uint8
	Mode         [Modes]Mode
}

// DefaultModes are the match defaults (docs/design.md, "Firing modes").
func DefaultModes() [Modes]Mode {
	return [Modes]Mode{
		ModeLight:   {DamageCode: 0, AutoReload: true, Magazine: 30, ReloadMs: 2000, FireIntervalMs: 250},
		ModeHeavy:   {DamageCode: 3, AutoReload: false, Magazine: 5, ReloadMs: 3000, FireIntervalMs: 1500},
		ModeShotgun: {DamageCode: 2, AutoReload: false, Magazine: 2, ReloadMs: 3500, FireIntervalMs: 1000},
	}
}

type State struct {
	Flags         uint8 // see the State* flags; bits 4-5 are the mode fired last
	Health        uint16
	Ammo          [Modes]uint16
	ReloadingMask uint8 // bit m: mode m is reloading
	BatteryPct    uint8
}

// LastMode is the firing mode used last (flag bits 4-5).
func (s State) LastMode() uint8 { return s.Flags >> 4 }

func stateFlagsOK(f uint8) bool { return f <= 0x2F && f>>4 <= 2 }

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
	if c.Player > 63 || c.Team > 7 {
		return nil, ErrRange
	}
	b := make([]byte, ConfigLen)
	b[0], b[1], b[2] = Version, c.Player, c.Team
	if c.FriendlyFire {
		b[3] = 1
	}
	binary.LittleEndian.PutUint16(b[4:], c.MaxHealth)
	binary.LittleEndian.PutUint16(b[6:], c.RespawnMs)
	copy(b[8:], c.DamageTable[:])
	for i, m := range c.Mode {
		if m.DamageCode > 7 {
			return nil, ErrRange
		}
		p := b[16+8*i:]
		p[0] = m.DamageCode
		if m.AutoReload {
			p[1] = 1
		}
		binary.LittleEndian.PutUint16(p[2:], m.Magazine)
		binary.LittleEndian.PutUint16(p[4:], m.ReloadMs)
		binary.LittleEndian.PutUint16(p[6:], m.FireIntervalMs)
	}
	return b, nil
}

func DecodeConfig(b []byte) (Config, error) {
	var c Config
	if err := check(b, ConfigLen); err != nil {
		return c, err
	}
	if b[1] > 63 || b[2] > 7 || b[3] > 1 {
		return c, ErrRange
	}
	for i := 0; i < Modes; i++ {
		if p := b[16+8*i:]; p[0] > 7 || p[1] > 1 {
			return c, ErrRange
		}
	}
	c.Player, c.Team, c.FriendlyFire = b[1], b[2], b[3] == 1
	c.MaxHealth = binary.LittleEndian.Uint16(b[4:])
	c.RespawnMs = binary.LittleEndian.Uint16(b[6:])
	copy(c.DamageTable[:], b[8:])
	for i := range c.Mode {
		p := b[16+8*i:]
		c.Mode[i] = Mode{
			DamageCode:     p[0],
			AutoReload:     p[1] == 1,
			Magazine:       binary.LittleEndian.Uint16(p[2:]),
			ReloadMs:       binary.LittleEndian.Uint16(p[4:]),
			FireIntervalMs: binary.LittleEndian.Uint16(p[6:]),
		}
	}
	return c, nil
}

func (s State) Encode() ([]byte, error) {
	if !stateFlagsOK(s.Flags) || s.ReloadingMask > 7 || s.BatteryPct > 100 {
		return nil, ErrRange
	}
	b := make([]byte, StateLen)
	b[0], b[1] = Version, s.Flags
	binary.LittleEndian.PutUint16(b[2:], s.Health)
	for i, a := range s.Ammo {
		binary.LittleEndian.PutUint16(b[4+2*i:], a)
	}
	b[10], b[11] = s.ReloadingMask, s.BatteryPct
	return b, nil
}

func DecodeState(b []byte) (State, error) {
	var s State
	if err := check(b, StateLen); err != nil {
		return s, err
	}
	if !stateFlagsOK(b[1]) || b[10] > 7 || b[11] > 100 {
		return s, ErrRange
	}
	s.Flags, s.ReloadingMask, s.BatteryPct = b[1], b[10], b[11]
	s.Health = binary.LittleEndian.Uint16(b[2:])
	for i := range s.Ammo {
		s.Ammo[i] = binary.LittleEndian.Uint16(b[4+2*i:])
	}
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
