// Command lfsim plays simulated phones against a running server and checks
// the scoreboard (internal/sim).
package main

import (
	"context"
	"flag"
	"fmt"
	"os"

	"github.com/scshafe/lightfight/server/internal/match"
	"github.com/scshafe/lightfight/server/internal/sim"
)

func main() {
	var o sim.Options
	var mode string
	flag.StringVar(&o.Base, "server", "http://localhost:8080", "server base URL")
	flag.StringVar(&mode, "mode", "ffa", "ffa or tdm")
	flag.IntVar(&o.Players, "players", 4, "simulated players")
	flag.IntVar(&o.Teams, "teams", 2, "teams (tdm)")
	flag.IntVar(&o.Shots, "shots", 200, "shots to simulate")
	flag.IntVar(&o.Replays, "replays", 20, "reports sent twice, as after a reconnect")
	flag.Int64Var(&o.Seed, "seed", 1, "random seed")
	flag.BoolVar(&o.FriendlyFire, "friendly-fire", false, "tdm: allow hits on teammates")
	flag.Parse()
	o.Mode = match.Mode(mode)

	res, err := sim.Run(context.Background(), o)
	if err != nil {
		fmt.Fprintln(os.Stderr, "FAIL:", err)
		os.Exit(1)
	}
	fmt.Printf("ok: %d reports sent (%d replays), match %s\n", res.Sent, res.Dups, res.Snapshot.ID)
	for _, p := range res.Snapshot.Players {
		fmt.Printf("  %-6s team %d  score %3d  hits %3d  downs %2d  taken %3d  deaths %2d\n",
			p.Callsign, p.Team, p.Score, p.Hits, p.Downs, p.HitsTaken, p.Deaths)
	}
}
