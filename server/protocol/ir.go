package protocol

// IRFrame is the payload of one shot (docs/ir-protocol.md).
type IRFrame struct {
	Shooter, Team, Damage, Seq uint8
}

// CRC8 is CRC-8, polynomial 0x07, init 0, no reflection, no final xor.
func CRC8(data []byte) uint8 {
	var crc uint8
	for _, d := range data {
		crc ^= d
		for i := 0; i < 8; i++ {
			if crc&0x80 != 0 {
				crc = crc<<1 ^ 0x07
			} else {
				crc <<= 1
			}
		}
	}
	return crc
}

func irCRC(payload uint16) uint8 { return CRC8([]byte{byte(payload >> 8), byte(payload)}) }

// Pack returns the 24-bit word: shooter6 team3 damage3 seq4 crc8.
func (f IRFrame) Pack() (uint32, error) {
	if f.Shooter > 63 || f.Team > 7 || f.Damage > 7 || f.Seq > 15 {
		return 0, ErrRange
	}
	p := uint16(f.Shooter)<<10 | uint16(f.Team)<<7 | uint16(f.Damage)<<4 | uint16(f.Seq)
	return uint32(p)<<8 | uint32(irCRC(p)), nil
}

// UnpackIR parses a 24-bit word, rejecting a bad CRC.
func UnpackIR(w uint32) (IRFrame, error) {
	if w>>24 != 0 {
		return IRFrame{}, ErrRange
	}
	p := uint16(w >> 8)
	if irCRC(p) != uint8(w) {
		return IRFrame{}, ErrBadCRC
	}
	return IRFrame{uint8(p >> 10), uint8(p>>7) & 7, uint8(p>>4) & 7, uint8(p) & 15}, nil
}
