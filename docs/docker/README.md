# AVA Docker build environment

The image supplies a Linux build environment. It is **not** an AVA runtime
sandbox: the container has the host-mounted checkout and build workspace, and
AVA itself is not run as a contained production service.

Run the following from any shell with Docker and Git. The commands derive every
host path from the checkout and keep build/cache state in a writable directory
outside the read-only source mount:

```sh
REPOROOT=$(git rev-parse --show-toplevel)
BUILDDIR=$(mktemp -d "${TMPDIR:-/tmp}/ava-docker-build.XXXXXXXX")
mkdir -p "$BUILDDIR/docker"
docker build -f "$REPOROOT/docs/docker/Dockerfile" -t ava-ubuntu24 "$REPOROOT/docs/docker"
```

## Run a persistent container

```sh
docker run -d --name ava-ubuntu24 \
  --user "$(id -u):$(id -g)" \
  --mount type=bind,src="$BUILDDIR/docker",dst=/work \
  --mount type=bind,src="$REPOROOT",dst=/reporoot,readonly \
  -w /reporoot \
  -e GITACHE_ROOT=/work/.gitache \
  -e CCACHE_DIR=/work/.ccache \
  -e REPOROOT=/reporoot \
  -e BUILDDIR=/work/build \
  ava-ubuntu24 sleep infinity
```

Stop it when finished, then remove the host build directory if it is no longer
needed:

```sh
docker rm -f ava-ubuntu24
rm -rf "$BUILDDIR"
```

While it is running, execute commands with:

```sh
docker exec -it ava-ubuntu24 bash -lc 'echo "$REPOROOT"'
```

## Configure, build, and test

Configure a normal tree and an optional sanitizer tree inside the mounted build
workspace:

```sh
docker exec ava-ubuntu24 bash -lc 'mkdir -p "$GITACHE_ROOT" "$CCACHE_DIR"'
docker exec ava-ubuntu24 bash -lc \
  'cmake -S "$REPOROOT" -B "$BUILDDIR" -DCMAKE_BUILD_TYPE=Release -GNinja --log-level=WARNING -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DAVA_BUILD_TESTS=ON'
docker exec ava-ubuntu24 bash -lc \
  'cmake -S "$REPOROOT" -B "$BUILDDIR-sanitize" -DCMAKE_BUILD_TYPE=Release -GNinja --log-level=WARNING -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON'
```

Use the repository runners so build and CTest concurrency are bounded and the
per-build-tree lock is honored. Do not run build and test commands concurrently
against the same tree.

```sh
docker exec ava-ubuntu24 bash -lc '"$REPOROOT/scripts/build.sh" --build-dir "$BUILDDIR"'
docker exec ava-ubuntu24 bash -lc '"$REPOROOT/scripts/run-tests.sh" --build-dir "$BUILDDIR"'
docker exec ava-ubuntu24 bash -lc '"$REPOROOT/scripts/build.sh" --build-dir "$BUILDDIR-sanitize" --jobs 2'
docker exec ava-ubuntu24 bash -lc '"$REPOROOT/scripts/run-tests.sh" --build-dir "$BUILDDIR-sanitize" --jobs 2'
```
