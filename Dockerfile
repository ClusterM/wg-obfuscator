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
COPY --from=build /src/wg-obfuscator ./wg-obfuscator
COPY wg-obfuscator.conf /etc/wg-obfuscator/wg-obfuscator.conf
ENTRYPOINT ["./wg-obfuscator", "-c", "/etc/wg-obfuscator/wg-obfuscator.conf"]
