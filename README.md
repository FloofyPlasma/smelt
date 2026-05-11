# smelt

A fast, simple build tool for C projects. Inspired by cargo. Just add a `smelt.toml`
> [!WARNING]
> Work in progress. Not ready for production use.

## Features

- Simple TOML manifest
- Incremental builds with content hashing 
- Parallel compilation
- Git and local dependency management 
- Debug/release profiles
- `compile_commands.json` generation for compatible LSP

## Installation
```bash
git clone https://github.com/floofyplasma/smelt
cd smelt
gcc src/main.c src/manifest.c src/build.c src/cache.c src/deps.c \
    src/compdb.c src/init.c src/clean.c vendor/xxhash.c vendor/tomlc17.c \
    -Isrc -Ivendor -std=c17 -o smelt
```

## Usage

```bash
smelt init          # scaffold new project
smelt build         # debug build (default)
smelt build release # release build
smelt run           # build and run
smelt clean         # remove build artifacts
smelt add <url> <file1> [file2 ...]  # add git dependency
smelt add --pkg-config <name>        # add pkg-config dependency
smelt add <local/path>               # add local smelt-aware dependency
```

## Dependencies

### Git dependency (header-only or source drop-in)

```bash
smelt add https://github.com/Cyan4973/xxHash xxhash.h xxhash.c
smelt add https://github.com/cktan/tomlc17 src/tomlc17. src/tomlc17.h
```

Files are copied to `vendor/`, `.c` files auto-added to build, `vendor/` auto-added to include paths.

### Local dependency (smelt-aware)

If a local library has its own `smelt.toml`, smelt reads it automatically:

```bash
smelt add ../mylib
```

Sources are compiled incrementally alongside your project.

## Roadmap

- [ ] Lockfile support
- [ ] `smelt update` command
- [ ] `smelt test` command
- [ ] Package registry with build recipes for some popular libs (SDL3, raylib, sqlite, etc.)

## License

MIT, see [LICENSE](LICENSE) for details.
