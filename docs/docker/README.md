# Creating docker image

```sh
$ docker build -f $REPOROOT/docs/docker/Dockerfile -t ava-ubuntu24 $REPOROOT/docs/docker
```

# Run persistent docker container

```sh
$ mkdir "$BUILDDIR/docker"
$ docker run -d --name ava-ubuntu24 \
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

To stop that container again run

```sh
$ docker rm -f ava-ubuntu24
```

# Running commands

While the persistent container is running, you can run commands in it with

```sh
$ docker exec -it ava-ubuntu24 bash -lc 'echo "$REPOROOT"'
```

# Configuring AVA

To configure AVA inside docker, run

```sh
$ docker exec ava-ubuntu24 bash -lc 'mkdir "$GITACHE_ROOT" "$CCACHE_DIR"'
$ docker exec ava-ubuntu24 bash -lc \
  'cmake -S "$REPOROOT" -B "$BUILDDIR" -DCMAKE_BUILD_TYPE=Release -GNinja --log-level=WARNING -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DAVA_BUILD_TESTS=ON'
$ docker exec -it ava-ubuntu24 bash -lc \
  'cmake -S "$REPOROOT" -B "$BUILDDIR-sanitize" -DCMAKE_BUILD_TYPE=Release -GNinja --log-level=WARNING -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON'
```

# Building AVA

```sh
$ docker exec ava-ubuntu24 bash -lc 'cmake --build "$BUILDDIR" --parallel 32'
$ docker exec ava-ubuntu24 bash -lc 'cmake --build "$BUILDDIR-sanitize" --parallel 32'
```

# Running tests

```sh
$ docker exec ava-ubuntu24 bash -lc 'ctest --test-dir "$BUILDDIR" --output-on-failure'
```
