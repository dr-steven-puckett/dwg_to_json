# CadSentinel DWG JSON Schema  
### Version: 1.0.0  
### Produced by: `dwg_inspect` (C++ CLI using LibreDWG)

---

## Overview

`dwg_inspect` converts AutoCAD DWG files into a **stable, structured, versioned JSON document** suitable for downstream use in the CadSentinel platform.

This schema defines:

- Global drawing metadata  
- Layer table information  
- Entity-level geometry, attributes, text  
- Handles and ownership graph  
- Summary statistics for analytics  

The schema is **guaranteed backward-compatible** within the same major version (1.x.x).

---

## Top-Level Structure

```json
{
  "file": "<string>",
  "schema_version": "1.0.0",
  "libredwg_version": {
    "major": <int>,
    "minor": <int>
  },
  "header": { ... },
  "layers": [ ... ],
  "entities": [ ... ],
  "summary": { ... }
}
```

---

# 1. Header Structure

Example:

```json
"header": {
  "version": 32,
  "codepage": 1252,
  "extents": {
    "model": { "xmin": 0.0, "ymin": 0.0, "xmax": 297.0, "ymax": 210.0 },
    "paper": { "xmin": 0.0, "ymin": 0.0, "xmax": 420.0, "ymax": 297.0 }
  }
}
```

## 1.1 Fields

| Field | Type | Description |
|-------|------|-------------|
| `version` | integer | DWG version code from LibreDWG. |
| `codepage` | integer | DWG codepage (e.g., 1252). |
| `extents` | object | Model & paper extents derived via LibreDWG APIs. |

## 1.2 Extents Object

Both `model` and `paper` contain:

- `xmin`  
- `ymin`  
- `xmax`  
- `ymax`

(All are numeric.)

---

# 2. Layers Structure

Example:

```json
{
  "name": "DIMENSIONS",
  "flags": 0,
  "lineweight": 25
}
```

## Layer Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Layer name. |
| `flags` | integer | Layer flags (frozen/locked/etc.). |
| `lineweight` | integer | Lineweight in DWG internal units. |

---

# 3. Entities Structure

Each element in `entities[]` represents a DWG entity in the file.

Example:

```json
{
  "index": 42,
  "type": "LINE",
  "raw_type": 13,
  "supertype": 2,
  "category": "curve",
  "layer": "0",
  "handle": "1A3",
  "owner_handle": "1B2",
  "geometry": { ... }
}
```

## 3.1 Common Fields (all entities)

| Field | Type | Description |
|--------|------|-------------|
| `index` | integer | DWG object index. |
| `type` | string | Human-readable DWG entity type. |
| `raw_type` | integer | DWG type enum value. |
| `supertype` | integer | DWG supertype. |
| `category` | string | `"curve"`, `"text"`, `"dimension"`, `"insert"`, `"block"`, `"image"`, `"other"`. |
| `layer` | string or null | Layer name or null if unknown. |
| `handle` | string or null | Hex DWG handle (`"1A3"`). |
| `owner_handle` | string or null | Parent object handle. |
| `geometry` | object (optional) | Geometry structure per entity type. |
| `text` | string (optional) | TEXT or MTEXT content. |
| `value` | number (optional) | Dimension measurement. |

---

# 4. Geometry Structures

Geometry appears under:

```json
"geometry": { ... }
```

The shape depends on entity type.

## 4.1 LINE

```json
"geometry": {
  "start": { "x": 0.0, "y": 0.0, "z": 0.0 },
  "end":   { "x": 10.0, "y": 0.0, "z": 0.0 }
}
```

## 4.2 CIRCLE

```json
"geometry": {
  "center": { "x": 100.0, "y": 50.0, "z": 0.0 },
  "radius": 25.0
}
```

## 4.3 ARC

```json
"geometry": {
  "center": { "x": 100.0, "y": 50.0, "z": 0.0 },
  "radius": 25.0,
  "start_angle": 0.0,
  "end_angle": 1.57
}
```

## 4.4 TEXT

```json
"geometry": {
  "ins_pt":  { "x": 10.0, "y": 5.0, "z": 0.0 },
  "height":  2.5,
  "rotation": 0.0
}
```

## 4.5 MTEXT

```json
"geometry": {
  "ins_pt": { "x": 25.0, "y": 40.0, "z": 0.0 }
}
```

## 4.6 LWPOLYLINE

```json
"geometry": {
  "vertices": [
    { "x": 0.0, "y": 0.0, "z": 0.0 },
    { "x": 10.0, "y": 0.0, "z": 0.0 }
  ],
  "closed": true
}
```

## 4.7 DIMENSION_LINEAR

```json
"geometry": {
  "definition_points": {
    "def_pt":  { "x": 0.0, "y": 0.0, "z": 0.0 },
    "xline1":  { "x": 0.0, "y": 0.0, "z": 0.0 },
    "xline2":  { "x": 25.0, "y": 0.0, "z": 0.0 }
  },
  "text_position": {
    "x": 12.5,
    "y": 5.0,
    "z": 0.0
  },
  "dim_rotation": 0.0,
  "text_rotation": 0.0
}
```

## 4.8 INSERT

```json
"geometry": {
  "ins_pt": { "x": 100.0, "y": 50.0, "z": 0.0 },
  "scale":  { "x": 1.0, "y": 1.0, "z": 1.0 },
  "rotation": 0.0
}
```

---

# 5. Summary Section

Example:

```json
"summary": {
  "num_objects": 245,
  "num_entities": 173,
  "entity_type_counts": {
    "LINE": 90,
    "TEXT": 40
  },
  "category_counts": {
    "curve": 103,
    "text": 40,
    "dimension": 12
  },
  "layer_counts": {
    "0": 80,
    "DIMENSIONS": 20
  }
}
```

## 5.1 Summary Fields

| Field | Type | Description |
|--------|------|-------------|
| `num_objects` | integer | Total DWG objects. |
| `num_entities` | integer | Total DWG entities. |
| `entity_type_counts` | object | Count per DWG type name. |
| `category_counts` | object | Count per high-level category. |
| `layer_counts` | object | Count per layer name. |

---

# End of Schema Document
