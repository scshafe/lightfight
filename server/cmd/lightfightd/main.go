// Command lightfightd is the lightfight match server.
package main

import (
	"context"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/scshafe/lightfight/server/internal/api"
	"github.com/scshafe/lightfight/server/internal/store"
)

func main() {
	addr := flag.String("addr", ":8080", "listen address (the tailnet side is the container's port mapping)")
	db := flag.String("db", "lightfight.db", "SQLite database file")
	flag.Parse()

	st, err := store.Open(*db)
	if err != nil {
		log.Fatalf("opening %s: %v", *db, err)
	}
	defer st.Close()
	srv, err := api.New(st, time.Now)
	if err != nil {
		log.Fatalf("loading matches: %v", err)
	}
	hs := &http.Server{Addr: *addr, Handler: srv.Handler(), ReadHeaderTimeout: 10 * time.Second}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	go func() {
		<-ctx.Done()
		shutdown, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		hs.Shutdown(shutdown)
	}()
	log.Printf("lightfightd listening on %s, database %s", *addr, *db)
	if err := hs.ListenAndServe(); err != http.ErrServerClosed {
		log.Fatal(err)
	}
}
