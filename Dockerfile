# Stage 1: Build
FROM ubuntu:latest AS builder

# Install necessary build tools and dependencies
RUN apt-get update && \
    apt-get install -y build-essential sudo git systemctl

# Add the current project to the container
COPY . /usr/src/app

# Set the working directory
WORKDIR /usr/src/app

# Build the project using make
RUN make

# Install the built project into the /install directory
RUN mkdir /install && \
    make install

# Stage 2: Runtime
FROM ubuntu:latest

# Copy installed files from builder stage
COPY --from=builder /usr/bin/wg-obfuscator /usr/bin/wg-obfuscator
COPY --from=builder /etc/wg-obfuscator.conf /etc/wg-obfuscator.conf

# Ensure necessary permissions
RUN chmod +x /usr/bin/wg-obfuscator

# Command to start the service
CMD ["wg-obfuscator", "-c", "/etc/wg-obfuscator.conf"]