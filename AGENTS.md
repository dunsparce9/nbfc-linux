# Repository Guidelines

## Project Structure & Module Organization

This is an Autotools-based C port of NoteBook FanControl. Core programs live in `src/`: `nbfc_service`, `nbfc`, `ec_probe`, and `test_model_config`. Client subcommands are under `src/client/`, generated model-config parser code is under `src/generated/`, and tests/assets are under `src/test/`. Hardware configuration data is JSON in `share/nbfc/configs/`, with legacy/source XML in `xml/`. Documentation is in `doc/`; shell completions in `completion/`; init/service templates in `etc/`; packaging in `pkgbuilds/`.

## Build, Test, and Development Commands

- `./autogen.sh && ./configure --prefix=/usr --sysconfdir=/etc`: generate and configure the build system.
- `make`: build binaries, docs, service files, and completions.
- `make BUILD=debug`: build with debug symbols and less optimization.
- `sudo make install`: install binaries, configs, manpages, completions, and the selected init integration.
- `make clean`: remove built binaries and generated artifacts.
- `./tools/config.py source > src/generated/model_config.generated.c` and `./tools/config.py header > src/generated/model_config.generated.h`: regenerate model-config parser files.
- `cd xml && make transform_to_json`: convert XML laptop configs to JSON.

## Coding Style & Naming Conventions

C code uses two-space indentation, K&R-style braces, and short early-return error handling. Public functions generally use module prefixes such as `Fan_Init`, `Status_Print`, or `AcpiCall_Parse`; private helpers are `static`. Keep headers paired with implementation files where practical. Use helper macros such as `my` from `src/macros.h` consistently with nearby code. Python tools are plain scripts with minimal dependencies.

## Testing Guidelines

Build first with `make`, then run focused tests manually. Use `src/test_model_config` for model configuration validation and JSON cases in `src/test/`. Protocol behavior is covered by `python3 tools/test-protocol.py`; XML conversion checks are in `src/test/test_xml2json.py`. For config validation changes, add or update JSON fixtures in numbered `src/test/` directories, for example `05-FanSpeed-greater-100.json`.

## Commit & Pull Request Guidelines

Recent commits use short imperative or descriptive subjects, often scoped by component, for example `Makefile: Use pkg-config for lua`, `Added Lua support`, or `Updated PKGBUILD`. Keep commits focused and include regenerated files when required. Pull requests should describe the behavior change, list build/test commands run, mention affected hardware/config files, and link related issues. Include logs for service, EC, or packaging changes when they help reviewers reproduce the result.

## Safety & Configuration Notes

Fan control and embedded-controller access can affect hardware behavior. Prefer read-only service testing (`sudo nbfc restart -r`) before write-mode changes, and avoid committing local machine-specific files from `/etc/nbfc` or runtime state.
