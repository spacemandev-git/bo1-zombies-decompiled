# web/Dockerfile - the web distribution: Bun server + built client + mods/ and data/main/ (+ the engine, optionally).
#
# Build context = the repository root:
#   docker build -f web/Dockerfile -t bo1-web .
#   docker build -f web/Dockerfile --build-arg BUILD_ENGINE=1 -t bo1-web .   # also compiles the engine (slow)
#   docker run -p 8080:8080 bo1-web                                          # http://localhost:8080/bo1/
#
# BUILD_ENGINE=1 runs `cmake --preset web && cmake --build --preset web` in the Emscripten SDK image (docs/web-port.md)
# and ships web/engine/bo1.{js,wasm} + build.json. With the default 0 the image has no engine: the site, lobby and
# relay work, /bo1/api/config reports engine.available = false. Stage selection by build argument needs BuildKit
# (Railway and current Docker use it), which also skips the engine stage when it is not selected.

ARG BUILD_ENGINE=0
ARG BUN_VERSION=1.3
# the Emscripten release the engine is developed with (~/emsdk); keep in step with docs/web-port.md
ARG EMSDK_VERSION=6.0.11

# ----- client bundle -----
FROM oven/bun:${BUN_VERSION} AS build
WORKDIR /app
COPY web/package.json web/bun.lock* web/
RUN cd web && if [ -f bun.lock ]; then bun install --frozen-lockfile; else bun install; fi
COPY web/ web/
COPY mods/ mods/
COPY data/ data/
RUN cd web && bun run build

# ----- engine (BUILD_ENGINE=1) -----
FROM emscripten/emsdk:${EMSDK_VERSION} AS engine-1
RUN apt-get -qq update \
    && apt-get -qq install -y --no-install-recommends ninja-build \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
# .git is not in the build context, so build.json gets the commit Railway builds (or ENGINE_VERSION) instead
ARG RAILWAY_GIT_COMMIT_SHA=""
ARG ENGINE_VERSION=""
RUN cmake --preset web \
    && cmake --build --preset web \
    && test -f web/engine/bo1.js && test -f web/engine/bo1.wasm \
    && version="${ENGINE_VERSION:-${RAILWAY_GIT_COMMIT_SHA}}" \
    && if [ -n "$version" ]; then \
         printf '{ "version": "%s", "built": "%s" }\n' "$version" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > web/engine/build.json; \
       fi \
    && mkdir -p /out \
    && cp -R web/engine /out/engine

# ----- no engine (BUILD_ENGINE=0) -----
FROM build AS engine-0
RUN mkdir -p /out/engine

FROM engine-${BUILD_ENGINE} AS engine

# ----- engine files + precompressed .gz/.br siblings -----
FROM build AS bundle
COPY --from=engine /out/engine/ web/engine/
RUN bun web/server/precompress.ts web/engine

# ----- runtime -----
FROM oven/bun:${BUN_VERSION}-slim AS runtime
WORKDIR /app
ENV NODE_ENV=production \
    BASE_PATH=/bo1 \
    PORT=8080
# root-owned and read-only for the server's user; the server writes nothing to disk
COPY --from=bundle /app/web/package.json web/package.json
COPY --from=bundle /app/web/shared/ web/shared/
COPY --from=bundle /app/web/server/ web/server/
COPY --from=bundle /app/web/dist/public/ web/dist/public/
COPY --from=bundle /app/web/engine/ web/engine/
COPY mods/ mods/
COPY data/main/ data/main/
USER bun
EXPOSE 8080
CMD ["bun", "web/server/index.ts"]
