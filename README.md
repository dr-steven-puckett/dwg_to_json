# CadSentinel – DWG Inspect (v1.0)

CadSentinel’s **DWG Inspect** (`dwg_inspect`) is a C++ command-line tool that reads AutoCAD DWG files using **LibreDWG** and exports a structured **JSON** representation of the drawing.

This JSON is the **canonical contract** for downstream tooling (databases, embeddings, ASTM “spell checker”, etc.). Version **1.0.0** focuses on robust extraction of core DWG entities and metadata.

---

## Features (v1.0.0)

- Reads DWG files via **LibreDWG**
- Outputs pretty-printed **JSON** to `stdout`
- Includes:
  - File metadata:
    - `file`
    - `schema_version`
    - `libredwg_version`
  - `header` information:
    - DWG version
    - Codepage
    - Model/paper extents (as available)
  - `layers[]`:
    - `name`
    - `flags`
    - `lineweight`
  - `entities[]`:
    - `index`
    - `type` (human-readable type name)
    - `raw_type` (integer)
    - `supertype` (integer)
    - `category` (e.g. `curve`, `text`, `dimension`, `insert`, `block`, `image`, `other`)
    - `layer`
    - `handle`, `owner_handle`
    - Optional `text`
    - Optional `value`
    - Optional `geometry` object (type-specific fields)
  - `summary`:
    - `num_objects`
    - `num_entities`
    - `entity_type_counts`
    - `category_counts`
    - `layer_counts`

This JSON schema is documented in:

- `docs/dwg_inspect_schema_v1.0.0.md`
- `docs/dwg_inspect_schema_v1.0.0.json`

---

## Roadmap (v1.1+)

Planned enhancements for **v1.1.0** (ASTM-focused):

- Add a top-level **`title_block`** object:
  - `found`, `layout`, `block_name`
  - Insertion transform (`ins_pt`, `scale`, `rotation`)
  - Title block attributes (`DWG_NO`, `PART_NO`, `TITLE`, `REV`, `MATERIAL`, `FINISH`, etc.)
  - Static notes text within the title block (e.g., default tolerances, units)
- Harden and extend **dimension** entity geometry (especially `DIMENSION_LINEAR`)
- Enrich `header` with units and default dimension style

Architectural-specific data (rooms, doors, windows, grids, etc.) will be part of a **future 2.x** line and is out of scope for v1.x.

---

## Requirements

### System

- Ubuntu (including **WSL2 Ubuntu**)
- C++17-capable compiler:
  - `g++` (via `build-essential`)
- CMake 3.15+ recommended

### Libraries

- **LibreDWG** (and headers)
- **nlohmann::json** (header-only library)

On Ubuntu, you can start with:

```bash
sudo apt update
sudo apt install -y \
    git \
    cmake \
    build-essential \
    pkg-config \
    libdwg-dev libdwg-tools


Building (Ubuntu / WSL)

From inside the repo:

# starting at the project root
mkdir -p build
cd build

cmake ..
make -j$(nproc)


If everything succeeds, you should see a dwg_inspect executable in build/:

ls
# ...
# dwg_inspect

Usage

Basic usage:

./dwg_inspect path/to/file.dwg > file.json


Example:

./dwg_inspect ../tests/dwgs/sample.dwg > ../tests/expected/sample.json


Input: sample.dwg

Output: JSON printed to stdout, redirected to sample.json

If the DWG cannot be read, the JSON will include an error field:

{
  "file": "path/to/file.dwg",
  "schema_version": "1.0.0",
  "libredwg_version": "0.x.x",
  "error": "Failed to read DWG"
}

