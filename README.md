# CadSentinel DWG Inspector  
**DWG → JSON Extraction Tool (v1.1)**  
Built using **LibreDWG** and **C++17**

---

## Overview

This tool extracts **all relevant geometric, annotation, layer, block, and metadata information** from an AutoCAD **DWG** file and exports it as a structured **JSON document**.  

It is designed for **CadSentinel**, where a downstream **Python ETL pipeline** ingests the JSON and performs:

- Standards “spell checking” against ASTM/ISO/GD&T definitions  
- Symbol/geometry validation  
- Rule-based QA  
- Vector embedding + database indexing  
- Drawing search and AI-assisted analysis

This C++ program has **one job only**:

> **Read a DWG file using LibreDWG and output a complete JSON representation to stdout.**

DXF conversion, PNG export, thumbnails, or PDF generation are **not part of this module** (those are handled later by the Python layer).

---

## Features (v1.1)

### ✔ DWG Entity Extraction
- Lines, circles, arcs, ellipses  
- Lightweight polylines  
- Text (TEXT, MTEXT, ATTRIB, ATTDEF)  
- Dimensions (linear, aligned, angular, radial, diameter, ordinate, arc, large-radius)  
- Solids, 3DFACE, traces  
- INSERTS and MINSERTS  
- Leaders & Multileaders  
- Images, underlays, rasters  
- Tables, hatches, polygons  

### ✔ UTF-8 Safe Text Handling
DWG files often store text in **Latin-1 / CP1252**.  
This tool safely converts all such strings to UTF-8 to avoid JSON serialization issues.

### ✔ Geometry Extraction
Standardized geometry schema per entity:
- Points arrays (start/end, vertices, positions)
- Radii, angles, heights, rotations
- Insertion points, scale, extrusion vectors
- Dimension definition points & text positions

### ✔ Layer + Block Summary
Layer metadata:
- Name, flags (frozen/locked), lineweight

Block definitions:
- Block name
- Handle
- Member entity indexes + handles

### ✔ Title Block Detection
A heuristic identifies the likely **title block insert**, scoring based on:
- Block name keywords ("TITLE", "BLOCK", "BORDER")
- Attributes present (more attributes → higher score)
- Block definition handle matching  
Full attributes & geometry are included in the JSON.

### ✔ Full File Summary
- Entity counts by type  
- Category counts  
- Layer usage counts  
- DWG version and codepage  
- Extents (model and paper space)

---

## Output Format

Running:

```bash
./dwg_inspect drawing.dwg
```

Produces prettified JSON:

```json
{
  "file": "drawing.dwg",
  "schema_version": "1.1.0",
  "libredwg_version": { "major": 0, "minor": 12 },
  "header": {
    "version": 27,
    "codepage": 1252,
    "extents": { ... }
  },
  "layers": [ ... ],
  "blocks": [ ... ],
  "entities": [ ... ],
  "summary": { ... },
  "title_block": { ... }
}
```

The Python ETL will capture this via stdout and ingest into CadSentinel’s database.

---

## Build Instructions

### Requirements
- **LibreDWG** (libredwg + libredwg-dev)
- **CMake ≥ 3.10**
- **C++17 compiler**
- **nlohmann/json** (header-only)

### Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

This produces:

```
./dwg_inspect
```

---

## Usage

```bash
./dwg_inspect <file.dwg> > output.json
```

Example:

```bash
./dwg_inspect gearbox.dwg > gearbox.json
```

If DWG loading fails:

```bash
Error: dwg_read_file failed for 'gearbox.dwg' with error code X
```

---

## Project Structure

```
/src
   main.cpp
   dwg_inspector.cpp
/include
   dwg_inspector.hpp
CMakeLists.txt
README.md   ← (this file)
```

---

## Notes on v1.1 Design Philosophy

- **No DXF conversion** inside C++  
- **No internal LibreDWG headers** (`bits.h`, `dwg_bits.h`, `out_dxf.h`)  
- Uses only the **stable public API**: `dwg.h`, `dwg_api.h`
- JSON-only output; no file writing  
- Safe across Linux / macOS LibreDWG installations  
- Perfect fit for Python ingestion + rule-based validation

---

## Roadmap for v1.2 (Python-side)

This C++ extractor connects to a planned Python layer that will:

1. Convert DWG → DXF (via `dwg2dxf`)
2. Generate PNG, thumbnails, and PDF
3. Store JSON + vector embeddings in Postgres/pgvector
4. Apply ASTM/GD&T rule maps for “spell checking”
5. Provide an AI search interface via FastAPI

The C++ portion is complete and stable for that workflow.

---

## License

LibreDWG is GPLv3; this component must comply accordingly.
