package protocol

import (
	"bufio"
	"encoding/hex"
	"errors"
	"os"
	"strconv"
	"strings"
	"testing"
)

// vectors yields the data lines of a shared vector file split on commas.
func vectors(t *testing.T, name string) [][]string {
	t.Helper()
	f, err := os.Open("../../protocol/" + name)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	var rows [][]string
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		rows = append(rows, strings.Split(line, ","))
	}
	return rows
}

func nums(t *testing.T, s []string) []uint64 {
	t.Helper()
	out := make([]uint64, len(s))
	for i, x := range s {
		v, err := strconv.ParseUint(x, 10, 64)
		if err != nil {
			t.Fatal(err)
		}
		out[i] = v
	}
	return out
}

func TestIRVectors(t *testing.T) {
	rows := vectors(t, "ir-vectors.csv")
	if len(rows) < 5 {
		t.Fatalf("only %d vectors", len(rows))
	}
	for _, r := range rows {
		v := nums(t, r[:4])
		want, err := strconv.ParseUint(r[4], 16, 32)
		if err != nil {
			t.Fatal(err)
		}
		f := IRFrame{uint8(v[0]), uint8(v[1]), uint8(v[2]), uint8(v[3])}
		w, err := f.Pack()
		if err != nil || uint64(w) != want {
			t.Errorf("%v: pack = %06X, %v; want %06X", f, w, err, want)
		}
		g, err := UnpackIR(uint32(want))
		if err != nil || g != f {
			t.Errorf("%06X: unpack = %v, %v; want %v", want, g, err, f)
		}
	}
}

func TestIRBitErrors(t *testing.T) {
	const base = 0x1539B9
	for i := 0; i < 24; i++ {
		for j := i; j < 24; j++ {
			w := uint32(base) ^ 1<<i
			if j != i {
				w ^= 1 << j
			}
			if _, err := UnpackIR(w); !errors.Is(err, ErrBadCRC) {
				t.Errorf("bits %d,%d: err = %v", i, j, err)
			}
		}
	}
	if _, err := (IRFrame{Shooter: 64}).Pack(); err != ErrRange {
		t.Errorf("shooter 64: %v", err)
	}
}

func TestBLEVectors(t *testing.T) {
	rows := vectors(t, "ble-vectors.csv")
	if len(rows) < 14 {
		t.Fatalf("only %d vectors", len(rows))
	}
	for _, r := range rows {
		want, err := hex.DecodeString(r[len(r)-1])
		if err != nil {
			t.Fatal(err)
		}
		v := append([]uint64{0}, nums(t, r[1:len(r)-1])...)
		var got []byte
		switch r[0] {
		case "config":
			c := Config{uint8(v[1]), uint8(v[2]), uint8(v[3]), v[9] != 0,
				uint16(v[4]), uint16(v[5]), uint16(v[6]), uint16(v[7]), uint16(v[8]), [8]uint8{}}
			for i := 0; i < 8; i++ {
				c.DamageTable[i] = uint8(v[10+i])
			}
			got, err = c.Encode()
			if d, derr := DecodeConfig(want); derr != nil || d != c {
				t.Errorf("config decode: %v, %v", d, derr)
			}
		case "state":
			s := State{uint8(v[1]), uint16(v[2]), uint16(v[3]), uint8(v[4])}
			got, err = s.Encode()
			if d, derr := DecodeState(want); derr != nil || d != s {
				t.Errorf("state decode: %v, %v", d, derr)
			}
		case "event":
			e := Event{uint8(v[1]), uint16(v[2]), uint8(v[3]), uint8(v[4]), uint8(v[5]), uint16(v[6]), uint32(v[7])}
			got, err = e.Encode()
			if d, derr := DecodeEvent(want); derr != nil || d != e {
				t.Errorf("event decode: %v, %v", d, derr)
			}
		case "ack":
			got = EncodeAck(uint16(v[1]))
			if d, derr := DecodeAck(want); derr != nil || d != uint16(v[1]) {
				t.Errorf("ack decode: %v, %v", d, derr)
			}
		case "command":
			got, err = EncodeCommand(uint8(v[1]))
			if d, derr := DecodeCommand(want); derr != nil || d != uint8(v[1]) {
				t.Errorf("command decode: %v, %v", d, derr)
			}
		default:
			t.Fatalf("unknown kind %q", r[0])
		}
		if err != nil || hex.EncodeToString(got) != strings.ToLower(r[len(r)-1]) {
			t.Errorf("%s: encode = %X, %v; want %X", r[0], got, err, want)
		}
	}
}

func TestBLERejects(t *testing.T) {
	cfg := make([]byte, ConfigLen)
	cfg[0] = Version
	if _, err := DecodeConfig(cfg); err != nil {
		t.Fatal(err)
	}
	if _, err := DecodeConfig(cfg[:ConfigLen-1]); err != ErrLength {
		t.Errorf("short: %v", err)
	}
	cfg[0] = 2
	if _, err := DecodeConfig(cfg); err != ErrVersion {
		t.Errorf("version: %v", err)
	}
	cfg[0], cfg[1] = Version, 64
	if _, err := DecodeConfig(cfg); err != ErrRange {
		t.Errorf("player: %v", err)
	}
	if _, err := DecodeState([]byte{1, 8, 0, 0, 0, 0, 0}); err != ErrRange {
		t.Errorf("state flags: %v", err)
	}
	if _, err := DecodeState([]byte{1, 0, 0, 0, 0, 0, 101}); err != ErrRange {
		t.Errorf("battery: %v", err)
	}
	if _, err := DecodeEvent([]byte{1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}); err != ErrRange {
		t.Errorf("kind: %v", err)
	}
	if _, err := DecodeCommand([]byte{1, 5}); err != ErrRange {
		t.Errorf("command: %v", err)
	}
	if _, err := EncodeCommand(0); err != ErrRange {
		t.Errorf("encode command: %v", err)
	}
	if _, err := (Event{Kind: 9}).Encode(); err != ErrRange {
		t.Errorf("encode event: %v", err)
	}
}
