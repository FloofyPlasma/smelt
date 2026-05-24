# smelt

A fast, simple build tool for C projects. Inspired by cargo. Just add a `smelt.toml`
> [!WARNING]
> Work in progress. Not ready for production use.

## Features

- Simple TOML manifest
- Incremental builds with content hashing 
- Registry, and pkg-config dependency management
- Lockfile support for dependency pinning
- Debug/release profiles
- `compile_commands.json` generation for compatible LSP

## Installation
```bash
git clone https://github.com/floofyplasma/smelt
cd smelt
cc -Isrc -Ivendor $(find src vendor -type f -name '*.c') $(pkg-config --cflags --libs libgit2) -o smelt
./smelt build
```

## Usage

```bash
smelt init                        # scaffold new project
smelt build                       # debug build (default)
smelt build -p release            # release build
smelt run                         # build and run
smelt clean                       # remove build artifacts
smelt update                      # re-resolve dependencies and update lockfile
smelt add <dependency> <version>  # add registry dependency
smelt add --pkg-config <name>     # add pkg-config dependency
```

## Dependencies

### Registry dependency

```bash
smelt add xxhash 0.8.3
smelt add tomlc17 1.0.0
```

Dependencies are cloned to a local cache, built incrementally, and their include paths applied automatically.

### pkg-config dependency

```bash
smelt add --pkg-config sdl2
```

Include flags and linker flags are automatically applied.

## Roadmap

- [ ] `smelt test` command
- [ ] Parallel compilation
- [ ] Local smelt-aware dependencies (`smelt add <path>`)
- [ ] Build recipes for some popular libs (SDL3, raylib, sqlite, etc.)

## License

MIT, see [LICENSE](LICENSE) for details.
