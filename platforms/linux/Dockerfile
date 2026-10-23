# A Linux build environment for hosts that are not Linux:
#   docker build -t aitd-linux platforms/linux
#   docker run --rm -v "$PWD":/src -w /src aitd-linux make linux
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential libx11-dev libasound2-dev xvfb xauth \
    && rm -rf /var/lib/apt/lists/*
