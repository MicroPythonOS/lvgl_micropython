# Contributing to the MicroPythonOS fork of lvgl_micropython

**Open pull requests against the `integration` branch** (the default branch),
not `main`. `main` stays close to the upstream lvgl_micropython layout;
`integration` is the curated branch that aggregates the MicroPythonOS-specific
deltas and is what MicroPythonOS builds consume.

Conventions:

- **Branch names**: `feat/<name>` or `fix/<name>` for focused changes;
  long-running work lives on `topic/<name>` branches (see the
  ["MicroPythonOS Branching Model"](README.md#micropythonos-branching-model)
  section in the README for the current list and the merge flow).
- **Build-time patches**: changes that MicroPythonOS applies to the
  MicroPython/lvgl sources at build time are committed as `.patch` files in
  the repo root (e.g. `unix_autoimport_main.patch`) and wired up in
  MicroPythonOS's `scripts/build_mpos.sh`. If your change is a patch like
  this, add the `.patch` file here and open a companion PR in
  [MicroPythonOS](https://github.com/MicroPythonOS/MicroPythonOS) that
  applies it.
- **Custom fonts**: regenerate via `lib_lvgl_src_font/regenerate_fonts.sh`
  (see `lib_lvgl_src_font/README.md`); don't hand-edit generated `.c` files.
- **`main` changes**: only cross-cutting documentation belongs there;
  everything else goes to `integration` or a topic branch.

Building and testing happens through the
[MicroPythonOS](https://github.com/MicroPythonOS/MicroPythonOS) super-repo
(`scripts/build_mpos.sh`), which pins this repository as a submodule — see
its docs for desktop/device build instructions. Heed the warning at the top
of this repo's README about submodule handling when cloning standalone.
