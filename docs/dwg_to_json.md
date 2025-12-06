1. docs/dwg_json_format.md
# CadSentinel DWG JSON Format (v1.1)

**Producer:** `dwg_inspect` C++ tool  
**Purpose:** Stable JSON envelope for CadSentinel ETL, search, and “spell checking” logic.  
**Schema version:** `"1.1.0"`

The JSON document produced by `dwg_inspect` represents a single DWG file and contains:

- Basic file and library metadata
- Header + extents
- Layer definitions
- Flattened entity list
- Block definitions with membership
- Title-block heuristics
- Summary statistics

---

## 1. Root Object

Top-level structure:

```jsonc
{
  "file": "example.dwg",
  "schema_version": "1.1.0",
  "libredwg_version": {
    "major": 0,
    "minor": 12
  },
  "header": { ... },
  "layers": [ ... ],
  "blocks": [ ... ],
  "entities": [ ... ],
  "summary": { ... },
  "title_block": { ... }
}

1.1 file (string)

Path or filename originally passed to dwg_inspect.
Leading/trailing single or double quotes are stripped.

1.2 schema_version (string)

Semantic-ish version of this JSON schema.
Current value: "1.1.0".

1.3 libredwg_version (object)

LibreDWG version used at runtime.

"libredwg_version": {
  "major": 0,
  "minor": 12
}


major (integer)

minor (integer)

1.4 header (object)

Basic DWG header info and 2D extents.

"header": {
  "version": 27,
  "codepage": 1252,
  "extents": {
    "model": { "xmin": 0.0, "ymin": 0.0, "xmax": 100.0, "ymax": 100.0 },
    "paper": { "xmin": 0.0, "ymin": 0.0, "xmax": 420.0, "ymax": 297.0 }
  }
}


version (integer): internal DWG version enum.

codepage (integer): DWG text codepage.

extents (object)

model (object)

paper (object)

Each extents object:

{
  "xmin": 0.0,
  "ymin": 0.0,
  "xmax": 100.0,
  "ymax": 100.0
}


All numeric fields are numbers (double).

2. Layers
"layers": [
  {
    "name": "0",
    "flags": 0,
    "lineweight": 25
  },
  ...
]


Each layer object:

name (string): Layer name, converted to UTF-8.

flags (integer): Frozen/locked/etc, from layer->flag.

lineweight (integer): Hundredths of mm (0/255 have special DWG meanings).

3. Entities

entities is a flat array of all DWG entities:

"entities": [
  {
    "index": 42,
    "type": "LINE",
    "raw_type": 13,
    "supertype": 2,
    "category": "curve",
    "layer": "DIM",
    "handle": "1A3",
    "owner_handle": "1A0",
    "geometry": { ... },

    // optional extra fields depending on entity type:
    "text": "M12 x 1.75",
    "tag": "PARTNO",
    "default_value": "N/A",
    "prompt": "Enter part number",
    "flags": 0,
    "lock_position": false,
    "block_name": "TITLE_BLOCK_A3",
    "path_type": 1,
    "annot_type": 0
  },
  ...
]

3.1 Common entity fields

All entities have:

index (integer)
DWG object index (obj->index).

type (string)
Human-readable entity type name, e.g. "LINE", "TEXT", "LWPOLYLINE", "DIMENSION_LINEAR", "INSERT", "UNKNOWN".

raw_type (integer)
Underlying DWG type constant (obj->type).

supertype (integer)
DWG supertype (obj->supertype) – typically DWG_SUPERTYPE_ENTITY.

category (string)
Coarse classification from entity_category(raw_type):

"curve"

"text"

"dimension"

"insert"

"block"

"image"

"other"

layer (string or null)
Entity’s layer name (UTF-8) or null if none.

handle (string or null)
DWG handle as hex string (e.g. "1A3") or null if zero.

owner_handle (string or null)
Owner handle (e.g. block definition, dictionary) as hex string or null.

geometry (object, optional)
Present for entities where geometry is extracted (lines, arcs, circles, LWPOLYLINE, POINT, SOLID, TEXT, MTEXT, LEADER, ATTRIB, ATTDEF, dimensions).

3.2 Geometry object

The geometry object is type-specific but follows a few common patterns.

3.2.1 Points

Whenever possible, a generic points array is derived:

"geometry": {
  "points": [
    { "x": 0.0, "y": 0.0, "z": 0.0 },
    { "x": 10.0, "y": 0.0, "z": 0.0 }
  ],
  ...
}


Each point object:

x (number)

y (number)

z (number)

This points array is:

LINE: [start, end]

LWPOLYLINE: vertices

SOLID: vertices

POINT: [position]

Others may also define points explicitly (e.g. LEADER polyline points).

3.2.2 Common geometry keys

Depending on entity type, geometry may contain:

start, end (LINE) – point objects

center (CIRCLE, ARC) – point object

radius (CIRCLE, ARC) – number

start_angle, end_angle (ARC) – number

position (POINT) – point object

thickness (POINT, SOLID) – number

x_ang (POINT) – number

vertices (LWPOLYLINE, SOLID) – array of points

closed (LWPOLYLINE) – boolean

elevation (SOLID) – number

extrusion (SOLID) – direction vector (point-like object)

Text-like entities:

ins_pt (TEXT, MTEXT, ATTRIB, ATTDEF) – insertion point

INSERT:

ins_pt – insertion point

scale – object { "x": ..., "y": ..., "z": ... }

rotation – number

Dimensions:

definition_points – object containing one or more of:

def_pt

xline1

xline2

feature_location

text_position – point object

dim_rotation – number (for linear dimensions)

text_rotation – number

horiz_dir – number (for some angular dimensions)

Leader:

points – polyline vertices

origin – point

endptproj – projected endpoint

x_direction – direction vector

inspt_offset – offset vector

dimgap, dimasz, box_height, box_width – numbers

arrowhead_on, hookline_on – booleans

3.3 Text / attribute fields

For certain entity types, extra fields live at the entity level:

TEXT, MTEXT:

text (string) – content

ATTRIB:

tag (string)

text (string) – value

flags (integer)

lock_position (boolean)

ATTDEF:

tag (string)

default_value (string)

prompt (string)

flags (integer)

lock_position (boolean)

3.4 INSERT fields

INSERT entities:

block_name (string, optional) – referenced block name (UTF-8).

3.5 LEADER fields

LEADER entities:

path_type (integer) – path style enum

annot_type (integer) – attached annotation type enum

3.6 Dimension fields

All dimension types (linear, aligned, angular, radial, etc.) may include:

text (string, optional) – user text override

value (number) – actual measurement (act_measurement)

4. Blocks

blocks describes block definitions and lists the entities belonging to each block.

"blocks": [
  {
    "name": "TITLE_BLOCK_A3",
    "handle": "1F2",
    "entity_indexes": [ 120, 121, 122 ],
    "entity_handles": [ "1F3", "1F4", "1F5" ],
    "num_entities": 3
  }
]


Fields:

name (string) – block name (UTF-8).

handle (string) – block definition handle.

entity_indexes (array of integers) – indexes into entities[].

entity_handles (array of string or null) – entity handles aligned with entity_indexes.

num_entities (integer) – count of member entities.

5. Summary

High-level counts and statistics:

"summary": {
  "num_objects": 512,
  "num_entities": 430,
  "entity_type_counts": {
    "LINE": 120,
    "LWPOLYLINE": 30,
    "TEXT": 80,
    "DIMENSION_LINEAR": 25
  },
  "category_counts": {
    "curve": 160,
    "text": 110,
    "dimension": 50,
    "insert": 20,
    "block": 10,
    "image": 2,
    "other": 78
  },
  "layer_counts": {
    "0": 50,
    "DIM": 40,
    "BORDER": 25,
    "<null>": 5
  }
}


Fields:

num_objects (integer) – total DWG objects.

num_entities (integer) – number of objects exported in entities.

entity_type_counts (object) – map of type → count.

category_counts (object) – map of category → count.

layer_counts (object) – map of layer name (or "<null>") → count.

6. Title Block

The title_block object exposes both:

A list of candidate title-block INSERTs.

The “best guess” title block.

"title_block": {
  "found": true,
  "block_name": "TITLE_BLOCK_A3",
  "handle": "200",
  "layer": "BORDER",
  "geometry": { ... },
  "attributes": {
    "DRAWING_NO": "123-456",
    "REV": "B",
    "SHEET": "1 OF 3"
  },
  "candidates": [
    {
      "handle": "1FF",
      "layer": "BORDER",
      "block_name": "TITLE_BLOCK_A4",
      "geometry": { ... },
      "attributes": {
        "DRAWING_NO": "123-456",
        "REV": "A"
      }
    },
    ...
  ]
}


Fields:

found (boolean) – whether a “best” title block was identified.

block_name (string or null) – selected block name.

handle (string or null) – selected INSERT handle.

layer (string or null) – selected INSERT layer.

geometry (object) – INSERT-level geometry (same schema as entities’ geometry).

attributes (object) – map of attribute TAG → value.

candidates (array) – list of candidate title-block INSERTs; each has:

handle (string)

layer (string or null)

block_name (string)

geometry (object)

attributes (object)

The scoring for found candidate favors:

Block names containing “TITLE”, “BLOCK”, or “BORDER” (case-insensitive).

Higher attribute count.

7. Versioning and Compatibility

schema_version = "1.1.0"

1.0.x → Initial JSON export.

1.1.0 → Added:

UTF-8 safe string conversion

Title-block extraction

Rich dimension + leader geometry

Coarse category field and points array normalization.

Future schema changes should:

Bump schema_version.

Be additive (avoid breaking existing fields).

Document changes here and in the JSON Schema file.


---

## 2. schema/dwg_inspect_v1.1.schema.json

Below is a JSON Schema (Draft 07) for validating outputs of `dwg_inspect` v1.1.  
I’m intentionally a bit flexible on “optional” properties so we don’t lock you out of minor future additions.

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://cadsentinel.local/schema/dwg_inspect_v1.1.schema.json",
  "title": "CadSentinel DWG Inspector JSON (v1.1)",
  "type": "object",
  "required": [
    "file",
    "schema_version",
    "libredwg_version",
    "header",
    "layers",
    "entities",
    "blocks",
    "summary",
    "title_block"
  ],
  "properties": {
    "file": {
      "type": "string"
    },
    "schema_version": {
      "type": "string",
      "const": "1.1.0"
    },
    "libredwg_version": {
      "type": "object",
      "required": [ "major", "minor" ],
      "properties": {
        "major": { "type": "integer" },
        "minor": { "type": "integer" }
      },
      "additionalProperties": false
    },
    "header": {
      "type": "object",
      "required": [ "version", "codepage", "extents" ],
      "properties": {
        "version": { "type": "integer" },
        "codepage": { "type": "integer" },
        "extents": {
          "type": "object",
          "required": [ "model", "paper" ],
          "properties": {
            "model": {
              "type": "object",
              "required": [ "xmin", "ymin", "xmax", "ymax" ],
              "properties": {
                "xmin": { "type": "number" },
                "ymin": { "type": "number" },
                "xmax": { "type": "number" },
                "ymax": { "type": "number" }
              },
              "additionalProperties": false
            },
            "paper": {
              "type": "object",
              "required": [ "xmin", "ymin", "xmax", "ymax" ],
              "properties": {
                "xmin": { "type": "number" },
                "ymin": { "type": "number" },
                "xmax": { "type": "number" },
                "ymax": { "type": "number" }
              },
              "additionalProperties": false
            }
          },
          "additionalProperties": false
        }
      },
      "additionalProperties": false
    },
    "layers": {
      "type": "array",
      "items": {
        "type": "object",
        "required": [ "name", "flags", "lineweight" ],
        "properties": {
          "name": { "type": "string" },
          "flags": { "type": "integer" },
          "lineweight": { "type": "integer" }
        },
        "additionalProperties": false
      }
    },
    "entities": {
      "type": "array",
      "items": {
        "type": "object",
        "required": [
          "index",
          "type",
          "raw_type",
          "supertype",
          "category",
          "layer",
          "handle",
          "owner_handle"
        ],
        "properties": {
          "index": { "type": "integer" },
          "type": { "type": "string" },
          "raw_type": { "type": "integer" },
          "supertype": { "type": "integer" },
          "category": {
            "type": "string",
            "enum": [
              "curve",
              "text",
              "dimension",
              "insert",
              "block",
              "image",
              "other"
            ]
          },
          "layer": {
            "type": [ "string", "null" ]
          },
          "handle": {
            "type": [ "string", "null" ]
          },
          "owner_handle": {
            "type": [ "string", "null" ]
          },
          "geometry": {
            "type": "object",
            "properties": {
              "start": { "$ref": "#/definitions/point3d" },
              "end": { "$ref": "#/definitions/point3d" },
              "center": { "$ref": "#/definitions/point3d" },
              "position": { "$ref": "#/definitions/point3d" },
              "ins_pt": { "$ref": "#/definitions/point3d" },
              "scale": {
                "type": "object",
                "properties": {
                  "x": { "type": "number" },
                  "y": { "type": "number" },
                  "z": { "type": "number" }
                },
                "additionalProperties": false
              },
              "extrusion": { "$ref": "#/definitions/point3d" },
              "x_direction": { "$ref": "#/definitions/point3d" },
              "inspt_offset": { "$ref": "#/definitions/point3d" },
              "radius": { "type": "number" },
              "start_angle": { "type": "number" },
              "end_angle": { "type": "number" },
              "thickness": { "type": "number" },
              "x_ang": { "type": "number" },
              "elevation": { "type": "number" },
              "rotation": { "type": "number" },
              "dim_rotation": { "type": "number" },
              "text_rotation": { "type": "number" },
              "horiz_dir": { "type": "number" },
              "dimgap": { "type": "number" },
              "dimasz": { "type": "number" },
              "box_height": { "type": "number" },
              "box_width": { "type": "number" },
              "arrowhead_on": { "type": "boolean" },
              "hookline_on": { "type": "boolean" },
              "vertices": {
                "type": "array",
                "items": { "$ref": "#/definitions/point3d" }
              },
              "points": {
                "type": "array",
                "items": { "$ref": "#/definitions/point3d" }
              },
              "definition_points": {
                "type": "object",
                "properties": {
                  "def_pt": { "$ref": "#/definitions/point3d" },
                  "xline1": { "$ref": "#/definitions/point3d" },
                  "xline2": { "$ref": "#/definitions/point3d" },
                  "feature_location": { "$ref": "#/definitions/point3d" }
                },
                "additionalProperties": true
              },
              "text_position": { "$ref": "#/definitions/point3d" },
              "closed": { "type": "boolean" }
            },
            "additionalProperties": true
          },
          "text": { "type": "string" },
          "tag": { "type": "string" },
          "default_value": { "type": "string" },
          "prompt": { "type": "string" },
          "flags": { "type": "integer" },
          "lock_position": { "type": "boolean" },
          "block_name": { "type": "string" },
          "path_type": { "type": "integer" },
          "annot_type": { "type": "integer" },
          "value": { "type": "number" }
        },
        "additionalProperties": true
      }
    },
    "blocks": {
      "type": "array",
      "items": {
        "type": "object",
        "required": [
          "name",
          "handle",
          "entity_indexes",
          "entity_handles",
          "num_entities"
        ],
        "properties": {
          "name": { "type": "string" },
          "handle": { "type": "string" },
          "entity_indexes": {
            "type": "array",
            "items": { "type": "integer" }
          },
          "entity_handles": {
            "type": "array",
            "items": { "type": [ "string", "null" ] }
          },
          "num_entities": { "type": "integer" }
        },
        "additionalProperties": false
      }
    },
    "summary": {
      "type": "object",
      "required": [
        "num_objects",
        "num_entities",
        "entity_type_counts",
        "category_counts",
        "layer_counts"
      ],
      "properties": {
        "num_objects": { "type": "integer" },
        "num_entities": { "type": "integer" },
        "entity_type_counts": {
          "type": "object",
          "additionalProperties": { "type": "integer" }
        },
        "category_counts": {
          "type": "object",
          "additionalProperties": { "type": "integer" }
        },
        "layer_counts": {
          "type": "object",
          "additionalProperties": { "type": "integer" }
        }
      },
      "additionalProperties": false
    },
    "title_block": {
      "type": "object",
      "required": [
        "found",
        "block_name",
        "handle",
        "layer",
        "geometry",
        "attributes",
        "candidates"
      ],
      "properties": {
        "found": { "type": "boolean" },
        "block_name": { "type": [ "string", "null" ] },
        "handle": { "type": [ "string", "null" ] },
        "layer": { "type": [ "string", "null" ] },
        "geometry": {
          "type": "object"
        },
        "attributes": {
          "type": "object",
          "additionalProperties": { "type": "string" }
        },
        "candidates": {
          "type": "array",
          "items": {
            "type": "object",
            "required": [ "handle", "layer", "block_name", "geometry", "attributes" ],
            "properties": {
              "handle": { "type": "string" },
              "layer": { "type": [ "string", "null" ] },
              "block_name": { "type": "string" },
              "geometry": {
                "type": "object"
              },
              "attributes": {
                "type": "object",
                "additionalProperties": { "type": "string" }
              }
            },
            "additionalProperties": true
          }
        }
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": false,
  "definitions": {
    "point3d": {
      "type": "object",
      "required": [ "x", "y", "z" ],
      "properties": {
        "x": { "type": "number" },
        "y": { "type": "number" },
        "z": { "type": "number" }
      },
      "additionalProperties": false
    }
  }
}
