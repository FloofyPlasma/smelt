# Smelt

Smelt is a small, declarative build system for C and C++ projects. You describe targets and dependencies in a `project.smelt` file, and Smelt finds the sources, compiles them with Clang, and links the result.

> [!WARNING]
> Smelt is still in early development. The format and command set may change.

## Requirements

- Linux
- `clang`, `clang++`, `ar`, and `git` on `PATH`

## Install

Smelt builds itself. On a machine without Smelt, run this from a checkout:

```sh
bootstrap/install.sh
```

The script builds Smelt in three stages and checks that the last two produce identical binaries before installing. The binary goes to `~/.local/bin/smelt`. No root is needed. Use `--prefix DIR` to install somewhere else.

Once Smelt is installed:

```sh
smelt install    # rebuild and replace the installed copy
smelt uninstall  # remove the installed copy
```

Make sure `~/.local/bin` is on your `PATH`. The install prints a note if it is not.

## A first project

```smelt
project "hello" {
    version = "0.1.0";
    cxx_std = 20;
}

target "app" {
    type = exe;
}
```

Put sources under `src/app`, then run:

```sh
smelt build
smelt run
```

Every `.c` and `.cpp` file under a target's source root is compiled. The root is `src/<target name>/` unless `source_dir` is set.

Editor support: [smelt-vim](https://github.com/FloofyPlasma/smelt-vim) provides syntax highlighting for Vim and Neovim.

## Dependencies

Dependencies can come from a local path, a git repository, or the Smelt registry.

```smelt
dependency "xxhash" {
    registry = "xxhash";
    version = "0.8.4";
    target = "xxhash";
}

target "app" {
    type = exe;
    depends = ["xxhash"];
}
```

Run `smelt fetch` once to resolve dependencies and write `smelt.lock`. Commit the lock file. Later builds use the locked revisions and do not contact the network. To move a locked dependency to a new version, run `smelt update NAME`.

## Contributing

TBD.

## License

MIT. See [LICENSE](LICENSE).

