# Stage 1: Build
FROM debian:latest AS build
ARG TARGETPLATFORM
WORKDIR /src
RUN apt-get update && apt-get install -y --no-install-recommends build-essential git
COPY ./. ./
RUN make clean && \
    if [ "$TARGETPLATFORM" = "linux/arm/v7" ]; then \
      make all CFLAGS="-march=armv7-a -mfpu=vfp -mfloat-abi=hard -O2 -Wall" LDFLAGS="-static"; \
    else \
      make all LDFLAGS="-static"; \
    fi

# Stage 2: Runtime
FROM scratch
WORKDIR /app
COPY --from=build /src/wg-obfuscator ./wg-obfuscator
COPY wg-obfuscator.conf /etc/wg-obfuscator/wg-obfuscator.conf
ENTRYPOINT ["./wg-obfuscator", "-c", "/etc/wg-obfuscator/wg-obfuscator.conf"]
