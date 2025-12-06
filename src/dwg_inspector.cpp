// dwg_inspector.cpp
#include "dwg_inspector.hpp"

#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <iostream>
#include <cstdlib>  // free()
#include <cstdio>   // snprintf for handle formatting
#include <cctype>   // std::toupper for title_block scoring

// LibreDWG is a C library; wrap includes in extern "C" for C++
extern "C" {
#include <stdint.h>
#include <dwg.h>
#include <dwg_api.h>
}

using json = nlohmann::json;

namespace {

// Convert a DWG/LibreDWG char* (likely in the DWG codepage, often Latin-1/CP1252)
// into a valid UTF-8 std::string. We treat bytes >= 0x80 as Latin-1 and
// encode them as 2-byte UTF-8 sequences. This avoids json UTF-8 errors.
std::string to_utf8_safe(const char* s) {
    if (!s) {
        return std::string();
    }
    std::string out;
    out.reserve(std::strlen(s) + 8);  // small bias for multi-byte chars

    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    while (*p) {
        unsigned char c = *p++;
        if (c < 0x80) {
            // ASCII, copy as-is
            out.push_back(static_cast<char>(c));
        } else {
            // Latin-1 -> UTF-8: 110xxxxx 10xxxxxx
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// Map DWG object type codes to human-readable names.
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

        // Faces / solids / regions
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
        case DWG_TYPE_WIPEOUT:               return "WIPEOUT";

        // Dictionaries, layout, etc.
        case DWG_TYPE_LAYOUT:                return "LAYOUT";
        case DWG_TYPE_DICTIONARY:            return "DICTIONARY";
        case DWG_TYPE_DICTIONARYVAR:         return "DICTIONARYVAR";
        case DWG_TYPE_GROUP:                 return "GROUP";

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
        case DWG_TYPE_POINT:   // if you added this earlier
            return "curve";

        // Text / annotation
        case DWG_TYPE_TEXT:
        case DWG_TYPE_ATTRIB:
        case DWG_TYPE_ATTDEF:
        case DWG_TYPE_MTEXT:
        case DWG_TYPE_MULTILEADER:
        case DWG_TYPE_TOLERANCE:
        case DWG_TYPE_MLINE:
        case DWG_TYPE_TABLE:
            return "text";

        // Dimensions (including LEADER now)
        case DWG_TYPE_DIMENSION_ORDINATE:
        case DWG_TYPE_DIMENSION_LINEAR:
        case DWG_TYPE_DIMENSION_ALIGNED:
        case DWG_TYPE_DIMENSION_ANG3PT:
        case DWG_TYPE_DIMENSION_ANG2LN:
        case DWG_TYPE_DIMENSION_RADIUS:
        case DWG_TYPE_DIMENSION_DIAMETER:
        case DWG_TYPE_ARC_DIMENSION:
        case DWG_TYPE_LARGE_RADIAL_DIMENSION:
        case DWG_TYPE_LEADER:    // <- moved here
            return "dimension";

        // Block references (inserts)
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
std::string handle_to_hex(const Dwg_Handle* h) {
    if (!h) {
        return std::string();
    }
    unsigned long long v = static_cast<unsigned long long>(h->value);
    if (v == 0ULL) {
        return std::string();
    }
    char buf[32];
    int len = std::snprintf(buf, sizeof(buf), "%llX", v);
    if (len <= 0) {
        return std::string();
    }
    return std::string(buf, static_cast<std::size_t>(len));
}

// Serialize a single LAYER object to JSON.
json layer_to_json(const Dwg_Object_LAYER* layer) {
    json j;
    if (!layer) {
        return j;
    }

    // Name
    j["name"] = layer->name ? to_utf8_safe(layer->name) : "";

    // Flags (frozen/locked/etc.)
    j["flags"] = static_cast<int>(layer->flag);

    // Lineweight (in hundredths of mm in DWG; 0/255 have special meanings)
    j["lineweight"] = static_cast<int>(layer->linewt);

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

        case DWG_TYPE_POINT: {
            Dwg_Entity_POINT* p = ent->tio.POINT;
            if (!p) break;

            // POINT has x, y, z, thickness, x_ang
            json pos;
            pos["x"] = p->x;
            pos["y"] = p->y;
            pos["z"] = p->z;
            geom["position"] = std::move(pos);

            geom["thickness"] = p->thickness;
            geom["x_ang"]     = p->x_ang;

            break;
        }

        case DWG_TYPE_SOLID: {
            Dwg_Entity_SOLID* s = ent->tio.SOLID;
            if (!s) break;

            // Four 2D corners (corner1..corner4) + elevation for Z
            json vertices = json::array();

            auto push_corner = [&vertices, s](const BITCODE_2RD& c) {
                json v;
                v["x"] = c.x;
                v["y"] = c.y;
                v["z"] = s->elevation;  // SOLID is 2D + elevation
                vertices.push_back(std::move(v));
            };

            push_corner(s->corner1);
            push_corner(s->corner2);
            push_corner(s->corner3);
            push_corner(s->corner4);

            geom["vertices"]  = std::move(vertices);
            geom["elevation"] = s->elevation;
            geom["thickness"] = s->thickness;

            // Extrusion direction (3D vector)
            json ext;
            ext["x"] = s->extrusion.x;
            ext["y"] = s->extrusion.y;
            ext["z"] = s->extrusion.z;
            geom["extrusion"] = std::move(ext);

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
            geom["start_angle"] = a->start_angle;
            geom["end_angle"]   = a->end_angle;
            break;
        }

        case DWG_TYPE_TEXT: {
            Dwg_Entity_TEXT* t = ent->tio.TEXT;
            if (!t) break;

            if (t->text_value) {
                ent_json["text"] = to_utf8_safe(t->text_value);
            }

            json ins_pt;
            ins_pt["x"] = t->ins_pt.x;
            ins_pt["y"] = t->ins_pt.y;
            ins_pt["z"] = 0.0;
            geom["ins_pt"] = std::move(ins_pt);

            geom["height"]   = t->height;
            geom["rotation"] = t->rotation;
            break;
        }

        case DWG_TYPE_MTEXT: {
            Dwg_Entity_MTEXT* mt = ent->tio.MTEXT;
            if (!mt) break;

            if (mt->text) {
                ent_json["text"] = to_utf8_safe(mt->text);
            }

            json ins_pt;
            ins_pt["x"] = mt->ins_pt.x;
            ins_pt["y"] = mt->ins_pt.y;
            ins_pt["z"] = 0.0;
            geom["ins_pt"] = std::move(ins_pt);

            break;
        }

        case DWG_TYPE_LEADER: {
            Dwg_Entity_LEADER* ld = ent->tio.LEADER;
            if (!ld) break;

            // Polyline points along the leader
            json pts = json::array();
            if (ld->num_points > 0 && ld->points) {
                for (BITCODE_BL i = 0; i < ld->num_points; ++i) {
                    json v;
                    v["x"] = ld->points[i].x;
                    v["y"] = ld->points[i].y;
                    v["z"] = ld->points[i].z;
                    pts.push_back(std::move(v));
                }
            }
            geom["points"] = std::move(pts);

            // Origin of the leader
            {
                json origin;
                origin["x"] = ld->origin.x;
                origin["y"] = ld->origin.y;
                origin["z"] = ld->origin.z;
                geom["origin"] = std::move(origin);
            }

            // Projected end point near the annotation
            {
                json endpt;
                endpt["x"] = ld->endptproj.x;
                endpt["y"] = ld->endptproj.y;
                endpt["z"] = ld->endptproj.z;
                geom["endptproj"] = std::move(endpt);
            }

            // Leader direction and text offset are useful for QA
            {
                json xdir;
                xdir["x"] = ld->x_direction.x;
                xdir["y"] = ld->x_direction.y;
                xdir["z"] = ld->x_direction.z;
                geom["x_direction"] = std::move(xdir);
            }
            {
                json offs;
                offs["x"] = ld->inspt_offset.x;
                offs["y"] = ld->inspt_offset.y;
                offs["z"] = ld->inspt_offset.z;
                geom["inspt_offset"] = std::move(offs);
            }

            // A few scalar properties that will help later spell-check rules
            geom["dimgap"]     = ld->dimgap;
            geom["dimasz"]     = ld->dimasz;
            geom["box_height"] = ld->box_height;
            geom["box_width"]  = ld->box_width;
            geom["arrowhead_on"] = (ld->arrowhead_on != 0);
            geom["hookline_on"]  = (ld->hookline_on  != 0);

            // Tagging leader type at the entity level
            ent_json["path_type"]  = static_cast<int>(ld->path_type);
            ent_json["annot_type"] = static_cast<int>(ld->annot_type);

            break;
        }

        case DWG_TYPE_ATTRIB: {
            Dwg_Entity_ATTRIB* a = ent->tio.ATTRIB;
            if (!a) break;

            // Tag + value
            const char* tag_cstr = a->tag
                ? reinterpret_cast<const char*>(a->tag)
                : nullptr;
            const char* val_cstr = a->text_value
                ? reinterpret_cast<const char*>(a->text_value)
                : nullptr;

            if (tag_cstr && *tag_cstr) {
                ent_json["tag"] = to_utf8_safe(tag_cstr);
            }
            if (val_cstr) {
                // Keep consistent with TEXT/MTEXT by using "text" for content
                ent_json["text"] = to_utf8_safe(val_cstr);
            }

            // Basic placement / size
            {
                json ins_pt;
                ins_pt["x"] = a->ins_pt.x;
                ins_pt["y"] = a->ins_pt.y;
                ins_pt["z"] = 0.0;  // 2D export; elevation is available separately if needed
                geom["ins_pt"] = std::move(ins_pt);
            }

            geom["height"]   = a->height;
            geom["rotation"] = a->rotation;

            // Useful metadata for later rules / spell-checking
            ent_json["flags"]          = static_cast<int>(a->flags);
            ent_json["lock_position"]  = (a->lock_position_flag != 0);

            break;
        }

        case DWG_TYPE_ATTDEF: {
            Dwg_Entity_ATTDEF* ad = ent->tio.ATTDEF;
            if (!ad) break;

            // Tag + definition details
            const char* tag_cstr = ad->tag
                ? reinterpret_cast<const char*>(ad->tag)
                : nullptr;
            const char* def_cstr = ad->default_value
                ? reinterpret_cast<const char*>(ad->default_value)
                : nullptr;
            const char* prompt_cstr = ad->prompt
                ? reinterpret_cast<const char*>(ad->prompt)
                : nullptr;

            if (tag_cstr && *tag_cstr) {
                ent_json["tag"] = to_utf8_safe(tag_cstr);
            }
            if (def_cstr) {
                ent_json["default_value"] = to_utf8_safe(def_cstr);
            }
            if (prompt_cstr) {
                ent_json["prompt"] = to_utf8_safe(prompt_cstr);
            }

            // Basic placement / size
            {
                json ins_pt;
                ins_pt["x"] = ad->ins_pt.x;
                ins_pt["y"] = ad->ins_pt.y;
                ins_pt["z"] = 0.0;
                geom["ins_pt"] = std::move(ins_pt);
            }

            geom["height"]   = ad->height;
            geom["rotation"] = ad->rotation;

            // Metadata
            ent_json["flags"]          = static_cast<int>(ad->flags);
            ent_json["lock_position"]  = (ad->lock_position_flag != 0);

            break;
        }

            case DWG_TYPE_INSERT: {
        Dwg_Entity_INSERT* ins = ent->tio.INSERT;
        if (!ins) break;

        // Block name referenced by this INSERT
        if (ins->block_name) {
            ent_json["block_name"] =
                to_utf8_safe(reinterpret_cast<const char*>(ins->block_name));
        }

        {
            json ins_pt;
            ins_pt["x"] = ins->ins_pt.x;
            ins_pt["y"] = ins->ins_pt.y;
            ins_pt["z"] = ins->ins_pt.z;
            geom["ins_pt"] = std::move(ins_pt);
        }
        {
            json scale;
            scale["x"] = ins->scale.x;
            scale["y"] = ins->scale.y;
            scale["z"] = ins->scale.z;
            geom["scale"] = std::move(scale);
        }
        geom["rotation"] = ins->rotation;
        break;
    }


        case DWG_TYPE_LWPOLYLINE: {
            Dwg_Entity_LWPOLYLINE* lw = ent->tio.LWPOLYLINE;
            if (!lw) break;

            json vertices = json::array();
            if (lw->num_points > 0 && lw->points) {
                for (int i = 0; i < static_cast<int>(lw->num_points); ++i) {
                    json v;
                    v["x"] = lw->points[i].x;
                    v["y"] = lw->points[i].y;
                    v["z"] = 0.0;
                    vertices.push_back(std::move(v));
                }
            }
            geom["vertices"] = std::move(vertices);

            bool closed = (lw->flag & 0x01) != 0;
            geom["closed"] = closed;
            break;
        }

        case DWG_TYPE_DIMENSION_LINEAR: {
            Dwg_Entity_DIMENSION_LINEAR* dl = ent->tio.DIMENSION_LINEAR;
            if (!dl) break;

            if (dl->user_text) {
                ent_json["text"] = to_utf8_safe(dl->user_text);
            }

            ent_json["value"] = dl->act_measurement;

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

            json text_pos;
            text_pos["x"] = dl->text_midpt.x;
            text_pos["y"] = dl->text_midpt.y;
            text_pos["z"] = 0.0;
            geom["text_position"] = std::move(text_pos);

            geom["dim_rotation"]  = dl->dim_rotation;
            geom["text_rotation"] = dl->text_rotation;

            break;
        }

        case DWG_TYPE_DIMENSION_ORDINATE: {
            Dwg_Entity_DIMENSION_ORDINATE* d = ent->tio.DIMENSION_ORDINATE;
            if (!d) break;

            if (d->user_text) {
                ent_json["text"] = to_utf8_safe(d->user_text);
            }

            ent_json["value"] = d->act_measurement;

            // Origin / definition point
            json def_pt;
            def_pt["x"] = d->def_pt.x;
            def_pt["y"] = d->def_pt.y;
            def_pt["z"] = d->def_pt.z;

            // Feature location (where the leader hits the feature)
            json feat_pt;
            feat_pt["x"] = d->feature_location_pt.x;
            feat_pt["y"] = d->feature_location_pt.y;
            feat_pt["z"] = d->feature_location_pt.z;

            json def_points;
            def_points["def_pt"]            = std::move(def_pt);
            def_points["feature_location"]  = std::move(feat_pt);
            geom["definition_points"]       = std::move(def_points);

            // Text placement
            json text_pos;
            text_pos["x"] = d->text_midpt.x;
            text_pos["y"] = d->text_midpt.y;
            text_pos["z"] = 0.0;
            geom["text_position"] = std::move(text_pos);

            geom["text_rotation"] = d->text_rotation;

            break;
        }

        case DWG_TYPE_DIMENSION_ALIGNED: {
            Dwg_Entity_DIMENSION_ALIGNED* d = ent->tio.DIMENSION_ALIGNED;
            if (!d) break;

            if (d->user_text) {
                ent_json["text"] = to_utf8_safe(d->user_text);
            }

            ent_json["value"] = d->act_measurement;

            // Definition point on dimension line
            json def_pt;
            def_pt["x"] = d->def_pt.x;
            def_pt["y"] = d->def_pt.y;
            def_pt["z"] = d->def_pt.z;
            geom["def_pt"] = std::move(def_pt);

            // Text placement
            json text_pos;
            text_pos["x"] = d->text_midpt.x;
            text_pos["y"] = d->text_midpt.y;
            text_pos["z"] = 0.0;
            geom["text_position"] = std::move(text_pos);

            geom["text_rotation"] = d->text_rotation;
            geom["horiz_dir"]     = d->horiz_dir;

            break;
        }

        case DWG_TYPE_DIMENSION_ANG3PT: {
            Dwg_Entity_DIMENSION_ANG3PT* d = ent->tio.DIMENSION_ANG3PT;
            if (!d) break;

            if (d->user_text) {
                ent_json["text"] = to_utf8_safe(d->user_text);
            }

            ent_json["value"] = d->act_measurement;

            json def_pt;
            def_pt["x"] = d->def_pt.x;
            def_pt["y"] = d->def_pt.y;
            def_pt["z"] = d->def_pt.z;
            geom["def_pt"] = std::move(def_pt);

            json text_pos;
            text_pos["x"] = d->text_midpt.x;
            text_pos["y"] = d->text_midpt.y;
            text_pos["z"] = 0.0;
            geom["text_position"] = std::move(text_pos);

            geom["text_rotation"] = d->text_rotation;
            geom["horiz_dir"]     = d->horiz_dir;

            break;
        }

        case DWG_TYPE_DIMENSION_ANG2LN: {
            Dwg_Entity_DIMENSION_ANG2LN* d = ent->tio.DIMENSION_ANG2LN;
            if (!d) break;

            if (d->user_text) {
                ent_json["text"] = to_utf8_safe(d->user_text);
            }

            ent_json["value"] = d->act_measurement;

            json def_pt;
            def_pt["x"] = d->def_pt.x;
            def_pt["y"] = d->def_pt.y;
            def_pt["z"] = d->def_pt.z;
            geom["def_pt"] = std::move(def_pt);

            json text_pos;
            text_pos["x"] = d->text_midpt.x;
            text_pos["y"] = d->text_midpt.y;
            text_pos["z"] = 0.0;
            geom["text_position"] = std::move(text_pos);

            geom["text_rotation"] = d->text_rotation;
            geom["horiz_dir"]     = d->horiz_dir;

            break;
        }

        case DWG_TYPE_DIMENSION_RADIUS: {
            Dwg_Entity_DIMENSION_RADIUS* d = ent->tio.DIMENSION_RADIUS;
            if (!d) break;

            if (d->user_text) {
                ent_json["text"] = to_utf8_safe(d->user_text);
            }

            ent_json["value"] = d->act_measurement;

            json def_pt;
            def_pt["x"] = d->def_pt.x;
            def_pt["y"] = d->def_pt.y;
            def_pt["z"] = d->def_pt.z;
            geom["def_pt"] = std::move(def_pt);

            json text_pos;
            text_pos["x"] = d->text_midpt.x;
            text_pos["y"] = d->text_midpt.y;
            text_pos["z"] = 0.0;
            geom["text_position"] = std::move(text_pos);

            geom["text_rotation"] = d->text_rotation;

            break;
        }

        case DWG_TYPE_DIMENSION_DIAMETER: {
            Dwg_Entity_DIMENSION_DIAMETER* d = ent->tio.DIMENSION_DIAMETER;
            if (!d) break;

            if (d->user_text) {
                ent_json["text"] = to_utf8_safe(d->user_text);
            }

            ent_json["value"] = d->act_measurement;

            json def_pt;
            def_pt["x"] = d->def_pt.x;
            def_pt["y"] = d->def_pt.y;
            def_pt["z"] = d->def_pt.z;
            geom["def_pt"] = std::move(def_pt);

            json text_pos;
            text_pos["x"] = d->text_midpt.x;
            text_pos["y"] = d->text_midpt.y;
            text_pos["z"] = 0.0;
            geom["text_position"] = std::move(text_pos);

            geom["text_rotation"] = d->text_rotation;

            break;
        }

        case DWG_TYPE_ARC_DIMENSION: {
            Dwg_Entity_ARC_DIMENSION* d = ent->tio.ARC_DIMENSION;
            if (!d) break;

            if (d->user_text) {
                ent_json["text"] = to_utf8_safe(d->user_text);
            }

            ent_json["value"] = d->act_measurement;

            json def_pt;
            def_pt["x"] = d->def_pt.x;
            def_pt["y"] = d->def_pt.y;
            def_pt["z"] = d->def_pt.z;
            geom["def_pt"] = std::move(def_pt);

            json text_pos;
            text_pos["x"] = d->text_midpt.x;
            text_pos["y"] = d->text_midpt.y;
            text_pos["z"] = 0.0;
            geom["text_position"] = std::move(text_pos);

            geom["text_rotation"] = d->text_rotation;

            break;
        }

        case DWG_TYPE_LARGE_RADIAL_DIMENSION: {
            Dwg_Entity_LARGE_RADIAL_DIMENSION* d = ent->tio.LARGE_RADIAL_DIMENSION;
            if (!d) break;

            if (d->user_text) {
                ent_json["text"] = to_utf8_safe(d->user_text);
            }

            ent_json["value"] = d->act_measurement;

            json def_pt;
            def_pt["x"] = d->def_pt.x;
            def_pt["y"] = d->def_pt.y;
            def_pt["z"] = d->def_pt.z;
            geom["def_pt"] = std::move(def_pt);

            json text_pos;
            text_pos["x"] = d->text_midpt.x;
            text_pos["y"] = d->text_midpt.y;
            text_pos["z"] = 0.0;
            geom["text_position"] = std::move(text_pos);

            geom["text_rotation"] = d->text_rotation;

            break;
        }


        default:
            break;
    }

        // Normalize geometry schema: derive a generic "points" array when possible
        if (!geom.empty()) {
            // If no "points" yet, try to infer them from other keys
            if (!geom.contains("points")) {
                json pts = json::array();

                // 1) LINE: start + end → 2 points
                if (geom.contains("start") && geom.contains("end")) {
                    pts.push_back(geom["start"]);
                    pts.push_back(geom["end"]);
                }
                // 2) Any entity with "vertices" (LWPOLYLINE, SOLID, etc.)
                else if (geom.contains("vertices") && geom["vertices"].is_array()) {
                    pts = geom["vertices"];
                }
                // 3) POINT-like entities with a single "position"
                else if (geom.contains("position")) {
                    pts.push_back(geom["position"]);
                }
                // 4) As a fallback for some text/dimension entities, you could
                //    choose to expose a single representative point later
                //    (e.g., ins_pt or text_position), but we keep it conservative
                //    for now to avoid guessing.

                if (!pts.empty()) {
                    geom["points"] = std::move(pts);
                }
            }

            ent_json["geometry"] = std::move(geom);
        }
    }

}


// Extract a candidate title block INSERT and its attributes from the DWG.
json extract_title_block(const Dwg_Data& dwg) {
    json result;
    result["found"] = false;
    result["block_name"] = nullptr;
    result["handle"] = nullptr;
    result["layer"] = nullptr;
    result["geometry"] = json::object();
    result["attributes"] = json::object();

    json candidates = json::array();
    std::unordered_map<std::string, std::size_t> handle_to_index;

    const std::size_t nobj = static_cast<std::size_t>(dwg.num_objects);

    // First pass: collect INSERT candidates with basic geometry.
    for (std::size_t i = 0; i < nobj; ++i) {
        Dwg_Object* obj = &dwg.object[i];

        if (obj->supertype != DWG_SUPERTYPE_ENTITY) {
            continue;
        }
        if (obj->type != DWG_TYPE_INSERT) {
            continue;
        }
        if (!obj->tio.entity || !obj->tio.entity->tio.INSERT) {
            continue;
        }
        Dwg_Entity_INSERT* ins = obj->tio.entity->tio.INSERT;

        json cand = json::object();

        // Handle as DWG hex string
        std::string handle_hex = handle_to_hex(&obj->handle);
        if (handle_hex.empty()) {
            continue; // we rely on handles for ATTRIB association
        }
        cand["handle"] = handle_hex;

        // Layer name
        const char* layer_name = nullptr;
        Dwg_Object_LAYER* layer = dwg_get_entity_layer(obj->tio.entity);
        if (layer && layer->name) {
            layer_name = layer->name;
        }
        if (layer_name) {
            cand["layer"] = to_utf8_safe(layer_name);
        } else {
            cand["layer"] = nullptr;
        }

        // Block name (BITCODE_TV)
        std::string block_name;
        if (ins->block_name) {
            block_name = to_utf8_safe(reinterpret_cast<const char*>(ins->block_name));
        }
        cand["block_name"] = block_name;

        // Basic geometry
        json geom = json::object();
        {
            json ins_pt;
            ins_pt["x"] = ins->ins_pt.x;
            ins_pt["y"] = ins->ins_pt.y;
            ins_pt["z"] = ins->ins_pt.z;
            geom["ins_pt"] = std::move(ins_pt);
        }
        {
            json scale;
            scale["x"] = ins->scale.x;
            scale["y"] = ins->scale.y;
            scale["z"] = ins->scale.z;
            geom["scale"] = std::move(scale);
        }
        geom["rotation"] = ins->rotation;
        cand["geometry"] = std::move(geom);

        cand["attributes"] = json::object();

        std::size_t idx = candidates.size();
        candidates.push_back(std::move(cand));
        handle_to_index.emplace(std::move(handle_hex), idx);
    }

    // Second pass: attach ATTRIBs to the owning INSERT via owner handle.
    for (std::size_t i = 0; i < nobj; ++i) {
        Dwg_Object* obj = &dwg.object[i];

        if (obj->supertype != DWG_SUPERTYPE_ENTITY) {
            continue;
        }
        if (obj->type != DWG_TYPE_ATTRIB) {
            continue;
        }
        if (!obj->tio.entity || !obj->tio.entity->tio.ATTRIB) {
            continue;
        }
        Dwg_Entity_ATTRIB* a = obj->tio.entity->tio.ATTRIB;

        std::string owner_hex;
        if (obj->tio.entity->ownerhandle) {
            owner_hex = handle_to_hex(&obj->tio.entity->ownerhandle->handleref);
        }
        if (owner_hex.empty()) {
            continue;
        }

        auto it = handle_to_index.find(owner_hex);
        if (it == handle_to_index.end()) {
            continue;
        }

        json& cand  = candidates[it->second];
        json& attrs = cand["attributes"];

        const char* tag_cstr = a->tag
            ? reinterpret_cast<const char*>(a->tag)
            : nullptr;
        const char* val_cstr = a->text_value
            ? reinterpret_cast<const char*>(a->text_value)
            : nullptr;

        if (tag_cstr && *tag_cstr) {
            std::string tag = to_utf8_safe(tag_cstr);
            std::string val = val_cstr ? to_utf8_safe(val_cstr) : std::string();
            attrs[tag] = val;
        }
    }

    result["candidates"] = candidates;

    // Choose best candidate
    int best_index = -1;
    int best_score = 0;

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const json& c = candidates[i];
        if (!c.contains("block_name") || !c["block_name"].is_string()) {
            continue;
        }
        const std::string name = c["block_name"].get<std::string>();
        std::string upper;
        upper.reserve(name.size());
        for (unsigned char ch : name) {
            upper.push_back(static_cast<char>(std::toupper(ch)));
        }

        bool has_title  = (upper.find("TITLE")  != std::string::npos);
        bool has_block  = (upper.find("BLOCK")  != std::string::npos);
        bool has_border = (upper.find("BORDER") != std::string::npos);

        int score = 0;
        if (has_title)  score += 3;
        if (has_block)  score += 2;
        if (has_border) score += 1;

        if (c.contains("attributes") && c["attributes"].is_object()) {
            score += static_cast<int>(c["attributes"].size());
        }

        if (score > best_score) {
            best_score = score;
            best_index = static_cast<int>(i);
        }
    }

    if (best_index >= 0) {
        const json& best = candidates[static_cast<std::size_t>(best_index)];
        result["found"]      = true;
        result["block_name"] = best.value("block_name", "");
        result["handle"]     = best.value("handle", json(nullptr));
        result["layer"]      = best.value("layer", json(nullptr));
        result["geometry"]   = best.value("geometry", json::object());
        result["attributes"] = best.value("attributes", json::object());
    }

    return result;
} // namespace


json DwgInspector::inspect(const std::string& dwg_path) {
    Dwg_Data dwg;
    std::memset(&dwg, 0, sizeof(Dwg_Data));

    int err = dwg_read_file(dwg_path.c_str(), &dwg);
    if (err >= DWG_ERR_CRITICAL) {
        throw std::runtime_error(
            "dwg_read_file failed for '" + dwg_path +
            "' with error code " + std::to_string(err)
        );
    }

    json root;

    // Basic file & library info
    // Clean input filename by stripping leading/trailing single or double quotes
    std::string clean_path = dwg_path;
    if (!clean_path.empty()) {
        if ((clean_path.front() == '"'  && clean_path.back() == '"') ||
            (clean_path.front() == '\'' && clean_path.back() == '\'')) 
        {
            clean_path = clean_path.substr(1, clean_path.size() - 2);
        }
    }

    root["file"] = clean_path;
    root["schema_version"] = "1.1.0";  // title_block + UTF-8-safe text
    root["libredwg_version"] = {
        {"major", LIBREDWG_VERSION_MAJOR},
        {"minor", LIBREDWG_VERSION_MINOR}
    };

    // Header (basic)
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

    // Layers
    json layers = json::array();
    BITCODE_BL layer_count = dwg_get_layer_count(&dwg);
    if (layer_count > 0) {
        Dwg_Object_LAYER** layer_array = dwg_get_layers(&dwg);
        if (layer_array) {
            for (BITCODE_BL i = 0; i < layer_count; ++i) {
                Dwg_Object_LAYER* layer = layer_array[i];
                if (!layer) continue;
                layers.push_back(layer_to_json(layer));
            }
            free(layer_array);
        }
    }
    root["layers"] = layers;

    // First pass: collect BLOCK definitions (handle -> name)
    std::unordered_map<std::string, std::string> block_definitions;

    for (std::size_t i = 0; i < static_cast<std::size_t>(dwg.num_objects); ++i) {
        Dwg_Object* obj = &dwg.object[i];

        if (obj->supertype != DWG_SUPERTYPE_ENTITY) {
            continue;
        }
        if (obj->type != DWG_TYPE_BLOCK) {
            continue;
        }
        if (!obj->tio.entity || !obj->tio.entity->tio.BLOCK) {
            continue;
        }

        Dwg_Entity_BLOCK* blk = obj->tio.entity->tio.BLOCK;

        // BLOCK.name is BITCODE_TV (char*), convert to UTF-8
        const char* name_cstr = blk->name
            ? reinterpret_cast<const char*>(blk->name)
            : nullptr;

        if (!name_cstr || !*name_cstr) {
            continue;
        }

        std::string name = to_utf8_safe(name_cstr);
        if (name.empty()) {
            continue;
        }

        // Use the same handle format as entities[]
        std::string handle_hex = handle_to_hex(&obj->handle);
        if (handle_hex.empty()) {
            continue;
        }

        block_definitions.emplace(std::move(handle_hex), std::move(name));
    }

    // Entities + summary
    json entities = json::array();
    std::unordered_map<std::string, std::size_t> type_counts;
    std::unordered_map<std::string, std::size_t> category_counts;
    std::unordered_map<std::string, std::size_t> layer_counts;
    std::size_t entity_count = 0;

    for (std::size_t i = 0; i < static_cast<std::size_t>(dwg.num_objects); ++i) {
        Dwg_Object* obj = &dwg.object[i];

        if (obj->supertype != DWG_SUPERTYPE_ENTITY) {
            continue;
        }

        ++entity_count;

        const int raw_type = static_cast<int>(obj->type);
        const std::string type_name = entity_type_name(raw_type);
        ++type_counts[type_name];

        json ent;
        ent["index"]     = static_cast<int>(obj->index);
        ent["type"]      = type_name;
        ent["raw_type"]  = raw_type;
        ent["supertype"] = static_cast<int>(obj->supertype);

        ent["category"]  = entity_category(raw_type);
        {
            const std::string cat = ent["category"].get<std::string>();
            ++category_counts[cat];
        }

        const char* layer_name = nullptr;
        if (obj->tio.entity) {
            Dwg_Object_Entity* ent_header = obj->tio.entity;
            Dwg_Object_LAYER* layer = dwg_get_entity_layer(ent_header);
            if (layer && layer->name) {
                layer_name = layer->name;
            }
        }
        if (layer_name) {
            ent["layer"] = to_utf8_safe(layer_name);
        } else {
            ent["layer"] = nullptr;
        }

        {
            std::string layer_key = layer_name ? layer_name : std::string("<null>");
            auto it = layer_counts.find(layer_key);
            if (it == layer_counts.end()) {
                layer_counts[layer_key] = 1;
            } else {
                ++(it->second);
            }
        }

        {
            std::string handle_hex = handle_to_hex(&obj->handle);
            if (!handle_hex.empty()) {
                ent["handle"] = handle_hex;
            } else {
                ent["handle"] = nullptr;
            }
        }

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

        add_geometry(ent, obj->tio.entity, raw_type);

        entities.push_back(std::move(ent));
    }

    root["entities"] = entities;

        // Build blocks[] summary:
    // One entry per BLOCK definition, listing child entities in that block.
    json blocks = json::array();

    for (const auto& kv : block_definitions) {
        const std::string& block_handle = kv.first;
        const std::string& block_name   = kv.second;

        json blk;
        blk["name"]   = block_name;
        blk["handle"] = block_handle;

        json entity_indexes = json::array();
        json entity_handles = json::array();

        // Find entities whose owner_handle == this BLOCK handle
        for (const auto& ent : entities) {
            if (!ent.contains("owner_handle") || ent["owner_handle"].is_null()) {
                continue;
            }
            if (!ent["owner_handle"].is_string()) {
                continue;
            }
            const std::string owner = ent["owner_handle"].get<std::string>();
            if (owner == block_handle) {
                // DWG object index (already exported as "index")
                entity_indexes.push_back(ent["index"]);

                // Entity handle (if present)
                if (ent.contains("handle") && ent["handle"].is_string()) {
                    entity_handles.push_back(ent["handle"]);
                } else {
                    entity_handles.push_back(nullptr);
                }
            }
        }

        blk["entity_indexes"] = std::move(entity_indexes);
        blk["entity_handles"] = std::move(entity_handles);
        blk["num_entities"]   =
            static_cast<std::uint64_t>(blk["entity_indexes"].size());

        blocks.push_back(std::move(blk));
    }

    root["blocks"] = std::move(blocks);

    json summary;
    summary["num_objects"]        = static_cast<std::uint64_t>(dwg.num_objects);
    summary["num_entities"]       = static_cast<std::uint64_t>(entity_count);
    summary["entity_type_counts"] = type_counts;
    summary["category_counts"]    = category_counts;
    summary["layer_counts"]       = layer_counts;
    root["summary"]               = summary;

    root["title_block"] = extract_title_block(dwg);

    dwg_free(&dwg);
    return root;
}
