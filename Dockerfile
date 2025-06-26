# Stage 1: Build
FROM alpine:latest AS build
WORKDIR /src
RUN apk add --no-cache build-base argp-standalone git
COPY ./. ./
RUN make clean && make LDFLAGS="-largp -static"

# Stage 2: Runtime
FROM scratch
WORKDIR /app
COPY --from=build /src/wg-obfuscator ./wg-obfuscator
COPY wg-obfuscator.conf /etc/wg-obfuscator/wg-obfuscator.conf
ENTRYPOINT ["./wg-obfuscator", "-c", "/etc/wg-obfuscator/wg-obfuscator.conf"]
