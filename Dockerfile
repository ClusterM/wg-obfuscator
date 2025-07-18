# Stage 1: Build
FROM alpine:latest AS build
WORKDIR /src
RUN apk add --no-cache build-base git
COPY ./. ./
RUN make clean && \
    if [ "$TARGETPLATFORM" = "linux/arm/v7" ]; then \
      make all CFLAGS="-march=armv7-a -mfpu=vfp -mfloat-abi=hard" LDFLAGS="-static"; \
    else \
      make all LDFLAGS="-static"; \
    fi

# Stage 2: Runtime
FROM scratch
WORKDIR /app
COPY --from=build /src/wg-obfuscator ./wg-obfuscator
COPY wg-obfuscator.conf /etc/wg-obfuscator/wg-obfuscator.conf
ENTRYPOINT ["./wg-obfuscator", "-c", "/etc/wg-obfuscator/wg-obfuscator.conf"]
