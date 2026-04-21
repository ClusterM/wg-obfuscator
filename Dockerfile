# Stage 1: Build
FROM debian:latest AS build
ARG TARGETPLATFORM
ARG RELEASE
WORKDIR /src
RUN apt-get update && apt-get install -y --no-install-recommends build-essential git
COPY ./. ./
RUN make clean && make all LDFLAGS="-static"

# Stage 2: Runtime
FROM scratch
WORKDIR /app
COPY --from=build /src/wg0-obfuscator ./wg0-obfuscator
COPY wg0-obfuscator.conf /etc/wg0-obfuscator/wg0-obfuscator.conf
ENTRYPOINT ["./wg0-obfuscator", "-c", "/etc/wg0-obfuscator/wg0-obfuscator.conf"]
