# The match server image. Build from this directory:
#   docker build -t lightfight-server .
# The database lives in /data (mount a volume); the server listens on :8080 and
# is published on the tailnet by the deploy, never on the public internet.
FROM golang:1.26 AS build
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/lightfightd ./cmd/lightfightd \
 && CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/lfsim ./cmd/lfsim \
 && mkdir /out/data

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/lightfightd /out/lfsim /usr/local/bin/
# /data owned by the nonroot user (65532), so a fresh volume is writable
COPY --from=build --chown=65532:65532 /out/data /data
VOLUME /data
EXPOSE 8080
USER nonroot
ENTRYPOINT ["lightfightd", "-addr", ":8080", "-db", "/data/lightfight.db"]
