# syntax=docker/dockerfile:1
FROM ubuntu:26.04 AS build

ARG TARGETARCH
ARG BUILD_JOBS=3

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake curl git libarchive-dev \
        libboost-dev libssl-dev ninja-build patch \
    && rm -rf /var/lib/apt/lists/*

# Match the standalone Piper runtime used by TextLT's voice registry.
RUN case "$TARGETARCH" in \
        amd64) piper_arch=x86_64 ;; \
        arm64) piper_arch=aarch64 ;; \
        *) echo "Unsupported Piper architecture: $TARGETARCH" >&2; exit 1 ;; \
    esac \
    && curl --fail --silent --show-error --location --retry 3 \
        -o /tmp/piper.tar.gz \
        "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_${piper_arch}.tar.gz" \
    && mkdir /opt/piper \
    && tar -xzf /tmp/piper.tar.gz -C /opt/piper --strip-components=1 \
    && rm /tmp/piper.tar.gz

WORKDIR /src
COPY --exclude=docs . .
RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DTEXTLT_FETCH_BOOST=OFF \
    && cmake --build /build --target textlt textlt-piper-server --parallel "$BUILD_JOBS" \
    && DESTDIR=/stage cmake --install /build --strip \
    && install -Dm644 LICENSE /stage/usr/local/share/textlt/LICENSE

FROM scratch AS conversion-assets
COPY docs/docker/ /

FROM ubuntu:26.04 AS runtime

ARG TEXTLT_UID=1000
ARG TEXTLT_GID=1000

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        alsa-utils ca-certificates curl git libarchive13t64 libgomp1 libssl3t64 \
        iproute2 jq libstdc++6 openssh-client pandoc poppler-utils pulseaudio-utils wl-clipboard xclip \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /stage/usr/local/bin/textlt /usr/local/bin/textlt
COPY --from=build /stage/usr/local/share/textlt/ /usr/local/share/textlt/
COPY --from=build /opt/piper/ /usr/local/share/textlt/piper/bin/piper-runtime/
COPY --from=conversion-assets --chmod=755 /textlt-convert /usr/local/bin/textlt-convert
COPY --from=conversion-assets /actions.json /usr/local/share/textlt/docker-actions.json

# A fresh named volume inherits the owner of /state from the image.
RUN test "$TEXTLT_UID" -gt 0 && test "$TEXTLT_GID" -gt 0 \
    && (getent group "$TEXTLT_GID" >/dev/null || groupadd -g "$TEXTLT_GID" textlt) \
    && if getent passwd "$TEXTLT_UID" >/dev/null; then \
        account="$(getent passwd "$TEXTLT_UID" | cut -d: -f1)"; \
        usermod -d /state -g "$TEXTLT_GID" "$account"; \
    else \
        useradd -u "$TEXTLT_UID" -g "$TEXTLT_GID" -d /state -M textlt; \
    fi \
    && install -d -o "$TEXTLT_UID" -g "$TEXTLT_GID" -m 0700 /state /state/.config \
    && install -d -o "$TEXTLT_UID" -g "$TEXTLT_GID" /work \
    && printf '%s\n' 'XDG_DOWNLOAD_DIR="/work/Downloads"' > /state/.config/user-dirs.dirs \
    && chown "$TEXTLT_UID:$TEXTLT_GID" /state/.config/user-dirs.dirs

COPY --chown=${TEXTLT_UID}:${TEXTLT_GID} docs/registries/piper_voices_index.json \
    /state/.local/share/textlt/registries/piper_voices_index.json
RUN install -o "$TEXTLT_UID" -g "$TEXTLT_GID" -m 0600 \
        /usr/local/share/textlt/docker-actions.json /state/.local/share/textlt/actions.json \
    && install -d /state/.local/share/textlt/piper/bin \
    && ln -s /usr/local/share/textlt/piper/bin/piper-runtime/piper /state/.local/share/textlt/piper/bin/piper \
    && ln -s /usr/local/share/textlt/piper/bin/textlt-piper-server /state/.local/share/textlt/piper/bin/textlt-piper-server \
    && chown -hR "$TEXTLT_UID:$TEXTLT_GID" /state/.local

ENV HOME=/state \
    LANG=C.UTF-8 \
    TERM=xterm-256color \
    TEXTLT_DATA_DIR=/usr/local/share/textlt \
    TEXTLT_INITIAL_WORKSPACE=/work \
    TEXTLT_CONTAINER=1
WORKDIR /work
USER ${TEXTLT_UID}:${TEXTLT_GID}
ENTRYPOINT ["textlt"]
