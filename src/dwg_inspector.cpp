// dwg_inspector.cpp
#include "dwg_inspector.hpp"

#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <iostream>
#include <cstdlib>  // free()
#include <cstdio>   // snprintf for handle formatting

// LibreDWG is a C library; wrap includes in extern "C" for C++
extern "C" {
#include <stdint.h>
#include <dwg.h>
#include <dwg_api.h>
}

using json = nlohmann::json;

namespace {

// Map DWG object type codes to human-readable names.
// We accept a plain int so we don't depend on BITCODE_* typedefs.
std::string entity_type_name(int type) {
    switch (type) {
        // Basic text & attributes
        case DWG_TYPE_TEXT:                  return "TEXT";
        case DWG_TYPE_ATTRIB:                return "ATTRIB";
        case DWG_TYPE_ATTDEF:                return "ATTDEF";

        // Block structure
        case DWG_TYPE_BLOCK:                 return "BLOCK";
        case DWG_TYPE_ENDBLK:                return "ENDBLK";
        case DWG_TYPE_SEQEND:                return "SEQEND";
        case DWG_TYPE_INSERT:                return "INSERT";
        case DWG_TYPE_MINSERT:               return "MINSERT";

        // Vertices & polylines
        case DWG_TYPE_VERTEX_2D:             return "VERTEX_2D";
        case DWG_TYPE_VERTEX_3D:             return "VERTEX_3D";
        case DWG_TYPE_VERTEX_MESH:           return "VERTEX_MESH";
        case DWG_TYPE_VERTEX_PFACE:          return "VERTEX_PFACE";
        case DWG_TYPE_VERTEX_PFACE_FACE:     return "VERTEX_PFACE_FACE";
        case DWG_TYPE_POLYLINE_2D:           return "POLYLINE_2D";
        case DWG_TYPE_POLYLINE_3D:           return "POLYLINE_3D";
        case DWG_TYPE_POLYLINE_MESH:         return "POLYLINE_MESH";
        case DWG_TYPE_POLYLINE_PFACE:        return "POLYLINE_PFACE";
        case DWG_TYPE_LWPOLYLINE:            return "LWPOLYLINE";

        // Basic geometry
        case DWG_TYPE_LINE:                  return "LINE";
        case DWG_TYPE_CIRCLE:                return "CIRCLE";
        case DWG_TYPE_ARC:                   return "ARC";
        case DWG_TYPE_POINT:                 return "POINT";
        case DWG_TYPE__3DFACE:               return "3DFACE";   // note leading underscore
        case DWG_TYPE_SOLID:                 return "SOLID";
        case DWG_TYPE_TRACE:                 return "TRACE";
        case DWG_TYPE_SHAPE:                 return "SHAPE";
        case DWG_TYPE_ELLIPSE:               return "ELLIPSE";
        case DWG_TYPE_SPLINE:                return "SPLINE";
        case DWG_TYPE__3DSOLID:              return "3DSOLID";  // note leading underscore
        case DWG_TYPE_RAY:                   return "RAY";

        // View / layout helpers
        case DWG_TYPE_VIEWPORT:              return "VIEWPORT";

        // Dimensions
        case DWG_TYPE_DIMENSION_ORDINATE:           return "DIMENSION_ORDINATE";
        case DWG_TYPE_DIMENSION_LINEAR:             return "DIMENSION_LINEAR";
        case DWG_TYPE_DIMENSION_ALIGNED:            return "DIMENSION_ALIGNED";
        case DWG_TYPE_DIMENSION_ANG3PT:             return "DIMENSION_ANG3PT";
        case DWG_TYPE_DIMENSION_ANG2LN:             return "DIMENSION_ANG2LN";
        case DWG_TYPE_DIMENSION_RADIUS:             return "DIMENSION_RADIUS";
        case DWG_TYPE_DIMENSION_DIAMETER:           return "DIMENSION_DIAMETER";
        case DWG_TYPE_ARC_DIMENSION:                return "ARC_DIMENSION";
        case DWG_TYPE_LARGE_RADIAL_DIMENSION:       return "LARGE_RADIAL_DIMENSION";

        // Annotation
        case DWG_TYPE_MTEXT:                 return "MTEXT";
        case DWG_TYPE_LEADER:                return "LEADER";
        case DWG_TYPE_MULTILEADER:           return "MULTILEADER";
        case DWG_TYPE_TOLERANCE:             return "TOLERANCE";
        case DWG_TYPE_MLINE:                 return "MLINE";
        case DWG_TYPE_TABLE:                 return "TABLE";

        // Hatches & fills
        case DWG_TYPE_HATCH:                 return "HATCH";
        case DWG_TYPE_MPOLYGON:              return "MPOLYGON";

        // Images / underlays / rasters
        case DWG_TYPE_IMAGE:                 return "IMAGE";
        case DWG_TYPE_PDFUNDERLAY:           return "PDFUNDERLAY";
        // If your headers also define these, you can add them too:
        // case DWG_TYPE_DGNUNDERLAY:        return "DGNUNDERLAY";
        // case DWG_TYPE_DWFUNDERLAY:        return "DWFUNDERLAY";
        case DWG_TYPE_WIPEOUT:               return "WIPEOUT";

        // Sectioning / visualization
        case DWG_TYPE_SECTIONOBJECT:         return "SECTIONOBJECT";
        case DWG_TYPE_LIGHT:                 return "LIGHT";

        // Proxy / custom entities
        case DWG_TYPE_PROXY_ENTITY:          return "PROXY_ENTITY";

        // Fallback / unused
        case DWG_TYPE_UNUSED:                return "UNUSED";

        default:
            return "UNKNOWN";
    }
}
// Classify entities into coarse categories for downstream querying.
std::string entity_category(int type) {
    switch (type) {
        // Curves / geometry
        case DWG_TYPE_LINE:
        case DWG_TYPE_CIRCLE:
        case DWG_TYPE_ARC:
        case DWG_TYPE_ELLIPSE:
        case DWG_TYPE_SPLINE:
        case DWG_TYPE_LWPOLYLINE:
        case DWG_TYPE_POLYLINE_2D:
        case DWG_TYPE_POLYLINE_3D:
        case DWG_TYPE_POLYLINE_MESH:
        case DWG_TYPE_POLYLINE_PFACE:
        case DWG_TYPE__3DFACE:
        case DWG_TYPE_SOLID:
        case DWG_TYPE_TRACE:
        case DWG_TYPE_SHAPE:
        case DWG_TYPE_RAY:
            return "curve";

        // Text / annotation
        case DWG_TYPE_TEXT:
        case DWG_TYPE_ATTRIB:
        case DWG_TYPE_ATTDEF:
        case DWG_TYPE_MTEXT:
        case DWG_TYPE_LEADER:
        case DWG_TYPE_MULTILEADER:
        case DWG_TYPE_TOLERANCE:
        case DWG_TYPE_MLINE:
        case DWG_TYPE_TABLE:
            return "text";

        // Dimensions
        case DWG_TYPE_DIMENSION_ORDINATE:
        case DWG_TYPE_DIMENSION_LINEAR:
        case DWG_TYPE_DIMENSION_ALIGNED:
        case DWG_TYPE_DIMENSION_ANG3PT:
        case DWG_TYPE_DIMENSION_ANG2LN:
        case DWG_TYPE_DIMENSION_RADIUS:
        case DWG_TYPE_DIMENSION_DIAMETER:
        case DWG_TYPE_ARC_DIMENSION:
        case DWG_TYPE_LARGE_RADIAL_DIMENSION:
            return "dimension";

        // Block references
        case DWG_TYPE_INSERT:
        case DWG_TYPE_MINSERT:
            return "insert";

        // Block/table definitions
        case DWG_TYPE_BLOCK:
        case DWG_TYPE_ENDBLK:
        case DWG_TYPE_SEQEND:
            return "block";

        // Images / underlays / raster-ish
        case DWG_TYPE_IMAGE:
        case DWG_TYPE_PDFUNDERLAY:
        case DWG_TYPE_WIPEOUT:
            return "image";

        default:
            return "other";
    }
}


// Convert a Dwg_Handle to a hex string (e.g. "1A3").
// If the handle is null or zero, returns an empty string.
std::string handle_to_hex(const Dwg_Handle* h) {
    if (!h) {
        return std::string();
    }
    unsigned long long v = static_cast<unsigned long long>(h->value);
    if (v == 0ULL) {
        return std::string();
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llX", v);
    return std::string(buf);
}

// Serialize a DWG layer to JSON.
json layer_to_json(const Dwg_Object_LAYER* layer) {
    json j = json::object();
    if (!layer) {
        return j;
    }

    // Name
    j["name"] = (layer->name ? layer->name : "");

    // Flags (frozen/locked/etc.)
    j["flags"] = static_cast<int>(layer->flag);

    // Lineweight (in hundredths of mm in DWG; 0/255 have special meanings)
    j["lineweight"] = static_cast<int>(layer->linewt);

    // Note: layer->color is BITCODE_CMC (_dwg_color), and layer->ltype is
    // a BITCODE_H handle. Properly resolving these requires table lookups,
    // so we omit them here for now to keep this serializer simple and stable.
    return j;
}

// Add per-entity geometry/content for key types.
void add_geometry(json& ent_json, Dwg_Object_Entity* ent, int raw_type) {
    if (!ent) {
        return;
    }

    json geom = json::object();

    switch (raw_type) {
        case DWG_TYPE_LINE: {
            Dwg_Entity_LINE* ln = ent->tio.LINE;
            if (!ln) break;

            json start;
            start["x"] = ln->start.x;
            start["y"] = ln->start.y;
            start["z"] = ln->start.z;
            geom["start"] = std::move(start);

            json end;
            end["x"] = ln->end.x;
            end["y"] = ln->end.y;
            end["z"] = ln->end.z;
            geom["end"] = std::move(end);

            break;
        }

        case DWG_TYPE_CIRCLE: {
            Dwg_Entity_CIRCLE* c = ent->tio.CIRCLE;
            if (!c) break;

            json center;
            center["x"] = c->center.x;
            center["y"] = c->center.y;
            center["z"] = c->center.z;
            geom["center"] = std::move(center);

            geom["radius"] = c->radius;
            break;
        }

        case DWG_TYPE_ARC: {
            Dwg_Entity_ARC* a = ent->tio.ARC;
            if (!a) break;

            json center;
            center["x"] = a->center.x;
            center["y"] = a->center.y;
            center["z"] = a->center.z;
            geom["center"] = std::move(center);

            geom["radius"] = a->radius;
            // LibreDWG stores angles in radians
            geom["start_angle"] = a->start_angle;
            geom["end_angle"]   = a->end_angle;
            break;
        }

        case DWG_TYPE_TEXT: {
            // Single-line TEXT
            Dwg_Entity_TEXT* t = ent->tio.TEXT;
            if (!t) break;

            // Text string content
            if (t->text_value) {
                ent_json["text"] = t->text_value;
            }

            // Insertion point: BITCODE_2DPOINT (x, y). We synthesize z = 0.0.
            json ins_pt;
            ins_pt["x"] = t->ins_pt.x;
            ins_pt["y"] = t->ins_pt.y;
            ins_pt["z"] = 0.0;
            geom["ins_pt"] = std::move(ins_pt);

            // Nominal text height & rotation
            geom["height"]   = t->height;
            geom["rotation"] = t->rotation;

            break;
        }

        case DWG_TYPE_MTEXT: {
            // Multi-line MTEXT
            Dwg_Entity_MTEXT* mt = ent->tio.MTEXT;
            if (!mt) break;

            // MTEXT content
            if (mt->text) {
                ent_json["text"] = mt->text;
            }

            // Insertion point: ins_pt (likely BITCODE_2DPOINT)
            json ins_pt;
            ins_pt["x"] = mt->ins_pt.x;
            ins_pt["y"] = mt->ins_pt.y;
            ins_pt["z"] = 0.0;
            geom["ins_pt"] = std::move(ins_pt);

            // Your Dwg_Entity_MTEXT in this LibreDWG build does not expose
            // height/rotation with the expected names, so we omit them for now.
            break;
        }

                case DWG_TYPE_INSERT: {
            // Block reference (INSERT)
            Dwg_Entity_INSERT* ins = ent->tio.INSERT;
            if (!ins) break;

            // Insertion point: usually BITCODE_3BD
            {
                json ins_pt;
                ins_pt["x"] = ins->ins_pt.x;
                ins_pt["y"] = ins->ins_pt.y;
                ins_pt["z"] = ins->ins_pt.z;
                geom["ins_pt"] = std::move(ins_pt);
            }

            // Scale factors: BITCODE_3BD scale (x, y, z)
            {
                json scale;
                scale["x"] = ins->scale.x;
                scale["y"] = ins->scale.y;
                scale["z"] = ins->scale.z;
                geom["scale"] = std::move(scale);
            }

            // Rotation angle (radians)
            geom["rotation"] = ins->rotation;

            // Note: We’re not yet resolving the referenced block name here.
            // That requires walking the block table / block_header handle,
            // which we can add later once CadSentinel’s block strategy is fixed.

            break;
        }

        case DWG_TYPE_LWPOLYLINE: {
            // Lightweight 2D polyline
            Dwg_Entity_LWPOLYLINE* lw = ent->tio.LWPOLYLINE;
            if (!lw) break;

            json vertices = json::array();

            // LibreDWG: BITCODE_BL num_points; BITCODE_2DPOINT *points;
            if (lw->points && lw->num_points > 0) {
                for (BITCODE_BL i = 0; i < lw->num_points; ++i) {
                    json v;
                    v["x"] = lw->points[i].x;
                    v["y"] = lw->points[i].y;
                    v["z"] = 0.0;  // 2D polyline, so z = 0.0
                    vertices.push_back(std::move(v));
                }
            }

            geom["vertices"] = std::move(vertices);

            // Closed flag: bit 0 set → closed.
            bool closed = (lw->flag & 0x01) != 0;
            geom["closed"] = closed;

            break;
        }

        case DWG_TYPE_DIMENSION_LINEAR: {
            // Linear dimension: use LibreDWG's _dwg_entity_DIMENSION_LINEAR layout
            Dwg_Entity_DIMENSION_LINEAR* dl = ent->tio.DIMENSION_LINEAR;
            if (!dl) break;

            // Top-level text: override / explicit dim text
            if (dl->user_text) {
                ent_json["text"] = dl->user_text;
            }

            // Top-level value: stored/actual measurement
            ent_json["value"] = dl->act_measurement;

            // Definition points
            json def_pt;
            def_pt["x"] = dl->def_pt.x;
            def_pt["y"] = dl->def_pt.y;
            def_pt["z"] = dl->def_pt.z;

            json xline1;
            xline1["x"] = dl->xline1_pt.x;
            xline1["y"] = dl->xline1_pt.y;
            xline1["z"] = dl->xline1_pt.z;

            json xline2;
            xline2["x"] = dl->xline2_pt.x;
            xline2["y"] = dl->xline2_pt.y;
            xline2["z"] = dl->xline2_pt.z;

            json def_points;
            def_points["def_pt"]  = std::move(def_pt);
            def_points["xline1"]  = std::move(xline1);
            def_points["xline2"]  = std::move(xline2);
            geom["definition_points"] = std::move(def_points);

            // Text position: text_midpt is BITCODE_2RD (x, y); z from elevation
            json text_pos;
            text_pos["x"] = dl->text_midpt.x;
            text_pos["y"] = dl->text_midpt.y;
            text_pos["z"] = dl->elevation;
            geom["text_position"] = std::move(text_pos);

            // Orientation angles (radians)
            geom["dim_rotation"]  = dl->dim_rotation;
            geom["text_rotation"] = dl->text_rotation;

            break;
        }

        default:
            break;
    }

    if (!geom.empty()) {
        ent_json["geometry"] = std::move(geom);
    }
}

} // namespace


json DwgInspector::inspect(const std::string& dwg_path) {
    Dwg_Data dwg;
    std::memset(&dwg, 0, sizeof(Dwg_Data));

    // Read DWG file using LibreDWG
    int err = dwg_read_file(dwg_path.c_str(), &dwg);
    if (err >= DWG_ERR_CRITICAL) {
        throw std::runtime_error(
            "dwg_read_file failed for '" + dwg_path +
            "' with error code " + std::to_string(err)
        );
    }

    json root;

    // Basic file & library info
    root["file"] = dwg_path;
    root["schema_version"] = "1.0.0";  // CadSentinel schema version
    root["libredwg_version"] = {
        {"major", LIBREDWG_VERSION_MAJOR},
        {"minor", LIBREDWG_VERSION_MINOR}
    };

    // Header information (version, codepage, extents)
    json header;
    header["version"]  = static_cast<int>(dwg.header.version);
    header["codepage"] = static_cast<int>(dwg.header.codepage);

    json extents;
    extents["model"] = {
        {"xmin", dwg_model_x_min(&dwg)},
        {"ymin", dwg_model_y_min(&dwg)},
        {"xmax", dwg_model_x_max(&dwg)},
        {"ymax", dwg_model_y_max(&dwg)}
    };
    extents["paper"] = {
        {"xmin", dwg_page_x_min(&dwg)},
        {"ymin", dwg_page_y_min(&dwg)},
        {"xmax", dwg_page_x_max(&dwg)},
        {"ymax", dwg_page_y_max(&dwg)}
    };
    header["extents"] = extents;

    root["header"] = header;

    // === Layers table ===
    json layers = json::array();
    BITCODE_BL layer_count = dwg_get_layer_count(&dwg);
    if (layer_count > 0) {
        Dwg_Object_LAYER** layer_array = dwg_get_layers(&dwg);
        if (layer_array) {
            for (BITCODE_BL i = 0; i < layer_count; ++i) {
                Dwg_Object_LAYER* layer = layer_array[i];
                if (!layer) {
                    continue;
                }
                layers.push_back(layer_to_json(layer));
            }
            // dwg_get_layers allocates the array; caller is responsible for freeing
            free(layer_array);
        }
    }
    root["layers"] = layers;

    // === Entity listing + counts ===
    json entities = json::array();
    std::unordered_map<std::string, std::size_t> type_counts;
    std::unordered_map<std::string, std::size_t> category_counts;
    std::unordered_map<std::string, std::size_t> layer_counts;

    std::size_t entity_count = 0;

    for (std::size_t i = 0; i < static_cast<std::size_t>(dwg.num_objects); ++i) {
        Dwg_Object* obj = &dwg.object[i];

        // Only keep true ENTITIES (ignore tables, dictionaries, etc.)
        if (obj->supertype != DWG_SUPERTYPE_ENTITY) {
            continue;
        }

        ++entity_count;

        const int raw_type = static_cast<int>(obj->type);

        const std::string type_name = entity_type_name(static_cast<int>(obj->type));
        ++type_counts[type_name];

        json ent;
        ent["index"]     = static_cast<int>(obj->index);
        ent["type"]      = type_name;
        ent["raw_type"]  = raw_type;
        ent["supertype"] = static_cast<int>(obj->supertype);

        // New: coarse category for easier downstream querying
        ent["category"]  = entity_category(raw_type);

        {
            const std::string cat = ent["category"].get<std::string>();
            ++category_counts[cat];
        }

        // Layer name for this entity (if resolvable)
        const char* layer_name = nullptr;
        if (obj->tio.entity) {
            Dwg_Object_Entity* ent_header = obj->tio.entity;
            Dwg_Object_LAYER* layer = dwg_get_entity_layer(ent_header);
            if (layer && layer->name) {
                layer_name = layer->name;
            }
        }
        if (layer_name) {
            ent["layer"] = layer_name;
        } else {
            ent["layer"] = nullptr;
        }

        // Count by layer (use "<null>" for entities without a resolved layer)
        {
            std::string layer_key;
            if (ent["layer"].is_null()) {
                layer_key = "<null>";
            } else {
                layer_key = ent["layer"].get<std::string>();
            }
            ++layer_counts[layer_key];
        }


        // Handle of this entity (as DWG hex string, e.g. "1A3").
        {
            std::string handle_hex = handle_to_hex(&obj->handle);
            if (!handle_hex.empty()) {
                ent["handle"] = handle_hex;
            } else {
                ent["handle"] = nullptr;
            }
        }

        // Owner handle (DXF 330), when available.
        {
            std::string owner_hex;
            if (obj->tio.entity && obj->tio.entity->ownerhandle) {
                owner_hex = handle_to_hex(&obj->tio.entity->ownerhandle->handleref);
            }
            if (!owner_hex.empty()) {
                ent["owner_handle"] = owner_hex;
            } else {
                ent["owner_handle"] = nullptr;
            }
        }

        // Geometry & text/content for supported types
                // Geometry & text/content for supported types
        add_geometry(ent, obj->tio.entity, static_cast<int>(obj->type));

        entities.push_back(std::move(ent));
    }

    root["entities"] = entities;

    json summary;
    summary["num_objects"]        = static_cast<std::uint64_t>(dwg.num_objects);
    summary["num_entities"]       = static_cast<std::uint64_t>(entity_count);
    summary["entity_type_counts"] = type_counts;
    summary["category_counts"]    = category_counts;
    summary["layer_counts"]       = layer_counts;

    root["summary"] = summary;


    // Always free before returning
    dwg_free(&dwg);

    return root;
}
