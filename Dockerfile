# The browser shell, and only the browser shell.
#
# This image is not the firmware and not the desktop product. It compiles the
# shared simulation to wasm32 and serves the two pages in web/ as static files;
# the keyboard image is built by QMK from firmware/rules.mk and never comes
# near this file.
#
# It lives at the repository root because that is where Dokku builds from, and
# because the build genuinely needs the root: the browser shell is a shell over
# firmware/sim and desktop/, so the context has to include them.

# ---- Build stage ----
# Debian's clang is built with every LLVM target, so the stock package
# cross-compiles to WebAssembly with no separate toolchain; lld supplies
# wasm-ld. This is the same pair the CI job installs.
FROM debian:trixie-slim AS build

RUN apt-get update \
    && apt-get install --yes --no-install-recommends clang lld make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Only what the browser build compiles. Copying the whole tree would put the
# firmware sources and the host daemon in a layer that has no use for them.
COPY firmware/sim ./firmware/sim
COPY firmware/sim_sources.mk ./firmware/sim_sources.mk
COPY desktop ./desktop
COPY web ./web

RUN make -C web

# Assemble exactly what gets published, by name. A wildcard would ship the C
# shim, the freestanding header, the Makefile and the parity harness -- none of
# which belong on a web server, and all of which are one `git clone` away for
# anyone who wants them.
RUN mkdir -p /site \
    && cp web/index.html web/canvases.html /site/ \
    && cp web/city.css web/canvases.css /site/ \
    && cp web/duel-city.js web/city-page.js web/canvases-page.js /site/ \
    && cp web/favicon.svg /site/ \
    && cp web/duel_city.wasm /site/

# ---- Serve stage ----
FROM nginx:alpine
COPY web/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /site /usr/share/nginx/html
EXPOSE 80
