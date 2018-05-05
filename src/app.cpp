#include <app.h>

#include <limits>

#define D2R (3.141592653589793f / 180.f)

namespace HexaLab {

    // PUBLIC

    bool App::import_mesh ( string path ) {
        delete mesh;
        mesh = new Mesh();
        // Load
        HL_LOG ( "Loading %s...\n", path.c_str() );
        vector<Vector3f> verts;
        vector<HexaLab::Index> indices;

        if ( !Loader::load ( path, verts, indices ) ) {
            return false;
        }

        // Build
        HL_LOG ( "Building...\n" );
        Builder::build ( *mesh, verts, indices );
        // Validate
        // HL_LOG ( "Validating...\n" );
        // Builder::validate ( *mesh );
        // Update stats
        float max = std::numeric_limits<float>::lowest();
        float min = std::numeric_limits<float>::max();
        float avg = 0;

        for ( size_t i = 0; i < mesh->edges.size(); ++i ) {
            MeshNavigator nav = mesh->navigate ( mesh->edges[i] );
            Vector3f edge = nav.vert().position - nav.flip_vert().vert().position;
            float len = edge.norm();

            if ( len < min ) {
                min = len;
            }

            if ( len > max ) {
                max = len;
            }

            avg += len;
        }

        avg /= mesh->edges.size();
        mesh_stats.min_edge_len = min;
        mesh_stats.max_edge_len = max;
        mesh_stats.avg_edge_len = avg;
        float avg_v = 0;
        Vector3f v[8];

        for ( size_t i = 0; i < mesh->hexas.size(); ++i ) {
            int j = 0;
            MeshNavigator nav = mesh->navigate ( mesh->hexas[i] );
            Vert& a = nav.vert();

            do {
                v[j++] = nav.vert().position;
                nav = nav.rotate_on_face();
            } while ( nav.vert() != a );

            nav = nav.rotate_on_hexa().rotate_on_hexa().flip_vert();
            Vert& b = nav.vert();

            do {
                v[j++] = nav.vert().position;
                nav = nav.rotate_on_face();
            } while ( nav.vert() != b );

            avg_v += QualityMeasureFun::volume ( v[4], v[5], v[6], v[7], v[0], v[1], v[2], v[3], nullptr );
        }

        avg_v /= mesh->hexas.size();
        mesh_stats.avg_volume = avg_v;
        mesh_stats.vert_count = mesh->verts.size();
        mesh_stats.hexa_count = mesh->hexas.size();
        mesh_stats.aabb = mesh->aabb;
        mesh_stats.quality_min = 0;
        mesh_stats.quality_max = 0;
        mesh_stats.quality_avg = 0;
        mesh_stats.quality_var = 0;
        // build quality buffers
        this->compute_hexa_quality();

        // notify filters
        for ( size_t i = 0; i < filters.size(); ++i ) {
            filters[i]->on_mesh_set ( *mesh );
        }

        // build buffers
        this->flag_models_as_dirty();
        //this->update_models();
        this->build_singularity_models();
        return true;
    }

    void App::enable_quality_color_mapping ( ColorMap::Palette e ) {
        this->color_map = ColorMap ( e );
        this->quality_color_mapping_enabled = true;
        this->flag_models_as_dirty();   // TODO update color only
    }

    void App::disable_quality_color_mapping() {
        this->quality_color_mapping_enabled = false;
        this->flag_models_as_dirty();   // TODO update color only
    }

    void App::set_quality_measure ( QualityMeasureEnum e ) {
        this->quality_measure = e;
        this->compute_hexa_quality();

        if ( this->is_quality_color_mapping_enabled() ) {
            this->flag_models_as_dirty();   // TODO update color only
        }
    }

    void App::set_default_outside_color ( float r, float g, float b ) {
        this->default_outside_color = Vector3f ( r, g, b );

        if ( !this->is_quality_color_mapping_enabled() ) {
            this->flag_models_as_dirty();
        }
    }

    void App::set_default_inside_color ( float r, float g, float b ) {
        this->default_inside_color = Vector3f ( r, g, b );

        if ( !this->is_quality_color_mapping_enabled() ) {
            this->flag_models_as_dirty();
        }
    }

    void App::add_filter ( IFilter* filter ) {
        this->filters.push_back ( filter );
        this->flag_models_as_dirty();
    }

    bool App::update_models() {
        if ( this->models_dirty_flag ) {
            this->build_surface_models();
            this->models_dirty_flag = false;
            return true;
        }

        return false;
    }


    void App::show_boundary_singularity ( bool do_show ) {
        this->do_show_boundary_singularity = do_show;
        this->flag_models_as_dirty();
    }
    void App::show_boundary_creases ( bool do_show ) {
        this->do_show_boundary_creases = do_show;
        this->flag_models_as_dirty();
    }


    // PRIVATE

    void App::compute_hexa_quality() {
        quality_measure_fun* fun = get_quality_measure_fun ( this->quality_measure );
        void* arg = nullptr;

        switch ( this->quality_measure ) {
            case QualityMeasureEnum::RSS:
            case QualityMeasureEnum::SHAS:
            case QualityMeasureEnum::SHES:
                arg = &this->mesh_stats.avg_volume;
                break;

            default:
                break;
        }

        float min = std::numeric_limits<float>::max();
        float max = -std::numeric_limits<float>::max();
        float sum = 0;
        float sum2 = 0;
        mesh->hexa_quality.resize ( mesh->hexas.size() );
        mesh->normalized_hexa_quality.resize ( mesh->hexas.size() );
        Vector3f v[8];

        for ( size_t i = 0; i < mesh->hexas.size(); ++i ) {
            int j = 0;
            MeshNavigator nav = mesh->navigate ( mesh->hexas[i] );
            Vert& a = nav.vert();

            do {
                v[j++] = nav.vert().position;
                nav = nav.rotate_on_face();
            } while ( nav.vert() != a );

            nav = nav.rotate_on_hexa().rotate_on_hexa().flip_vert();
            Vert& b = nav.vert();

            do {
                v[j++] = nav.vert().position;
                nav = nav.rotate_on_face();
            } while ( nav.vert() != b );

            float q = fun ( v[4], v[5], v[6], v[7], v[0], v[1], v[2], v[3], arg );
            mesh->hexa_quality[i] = q;

            if ( min > q ) {
                min = q;
            }

            if ( max < q ) {
                max = q;
            }

            sum += q;
            sum2 += q * q;
        }

        this->mesh_stats.quality_min = min;
        this->mesh_stats.quality_max = max;
        this->mesh_stats.quality_avg = sum / mesh->hexas.size();
        this->mesh_stats.quality_var = sum2 / mesh->hexas.size() - this->mesh_stats.quality_avg * this->mesh_stats.quality_avg;
        min = std::numeric_limits<float>::max();
        max = -std::numeric_limits<float>::max();

        for ( size_t i = 0; i < mesh->hexas.size(); ++i ) {
            float q = normalize_quality_measure ( this->quality_measure, mesh->hexa_quality[i], this->mesh_stats.quality_min, this->mesh_stats.quality_max );

            if ( min > q ) {
                min = q;
            }

            if ( max < q ) {
                max = q;
            }

            mesh->normalized_hexa_quality[i] = q;
        }

        this->mesh_stats.normalized_quality_min = min;
        this->mesh_stats.normalized_quality_max = max;
    }

    void App::build_singularity_models() {
        singularity_model.clear();

        // boundary_singularity_model.clear();
        // boundary_creases_model.clear();
        for ( size_t i = 0; i < mesh->edges.size(); ++i ) {
            MeshNavigator nav = mesh->navigate ( mesh->edges[i] );
            // -- Boundary check --
            // {
            //     bool boundary = false;
            //     Face& begin = nav.face();
            //     do {
            //         if (nav.is_face_boundary()) {
            //             boundary = true;
            //             break;
            //         }
            //         nav = nav.rotate_on_edge();
            //     } while(nav.face() != begin);
            //     if (boundary) {
            //         // Boundary singularity
            //         if (nav.incident_face_on_edge_num() != 2) {
            //             for (int n = 0; n < 2; ++n) {     // 2 verts that make the edge
            //                 boundary_singularity_model.wireframe_vert_pos.push_back(nav.vert().position);
            //                 boundary_singularity_model.wireframe_vert_color.push_back(Vector3f(0, 0, 1));
            //                 nav = nav.flip_vert();
            //             }
            //         }
            //         // Boundary Crease
            //         MeshNavigator nav2 = nav.flip_face();
            //         if (!nav2.is_face_boundary()) {
            //             nav2 = nav2.flip_hexa().flip_face();
            //             float dot = nav.face().normal.dot(nav2.face().normal);
            //             if (std::acos(dot) > 30 * D2R) {
            //                 for (int n = 0; n < 2; ++n) {     // 2 verts that make the edge
            //                     boundary_creases_model.wireframe_vert_pos.push_back(nav.vert().position);
            //                     boundary_creases_model.wireframe_vert_color.push_back(Vector3f(1, 0, 0));
            //                     nav = nav.flip_vert();
            //                 }
            //             }
            //         }
            //     }
            // }
            // -- Singularity check
            int face_count = nav.incident_face_on_edge_num();

            if ( face_count == 4 ) {
                continue;
            }

            if ( nav.edge().is_surface ) {
                continue;
            }

            // add edge
            for ( int j = 0; j < 2; ++j ) {
                singularity_model.wireframe_vert_pos.push_back ( mesh->verts[nav.dart().vert].position );
                nav = nav.flip_vert();
            }

            Vector3f color;

            switch ( face_count ) {
                case  3:
                    color = Vector3f ( 1, 0, 0 );
                    break;

                case  5:
                    color = Vector3f ( 0, 1, 0 );
                    break;

                default:
                    color = Vector3f ( 0, 0, 1 );
            }

            singularity_model.wireframe_vert_color.push_back ( color );
            singularity_model.wireframe_vert_color.push_back ( color );
            // add adjacent faces
            Face& begin = nav.face();

            do {                                          // foreach face adjacent tot he singularity edge
                for ( int k = 0; k < 2; ++k ) {           // for both triangles making up the face
                    for ( int j = 0; j < 2; ++j ) {       // 2 + 1 face vertices add
                        singularity_model.surface_vert_pos.push_back ( mesh->verts[nav.dart().vert].position );
                        singularity_model.surface_vert_color.push_back ( color );

                        for ( int n = 0; n < 2; ++n ) {   // 2 verts that make the edge
                            if ( j == 0 && k == 1 ) {
                                continue;
                            }

                            singularity_model.wireframe_vert_pos.push_back ( mesh->verts[nav.dart().vert].position );
                            singularity_model.wireframe_vert_color.push_back ( Vector3f ( 0, 0, 0 ) );
                            nav = nav.flip_vert();
                        }

                        nav = nav.rotate_on_face();
                    }

                    singularity_model.surface_vert_pos.push_back ( mesh->verts[nav.dart().vert].position );
                    singularity_model.surface_vert_color.push_back ( color );
                }

                nav = nav.rotate_on_edge();
            } while ( nav.face() != begin );
        }
    }

    void App::add_visible_vert ( Dart& dart, float normal_sign, Vector3f color ) {
        MeshNavigator nav = mesh->navigate ( dart );
        visible_model.surface_vert_pos.push_back ( nav.vert().position );
        visible_model.surface_vert_norm.push_back ( nav.face().normal * normal_sign );
        visible_model.surface_vert_color.push_back ( color );
        HL_ASSERT ( visible_model.surface_vert_pos.size() == visible_model.surface_vert_norm.size() &&
                    visible_model.surface_vert_pos.size() == visible_model.surface_vert_color.size() );
        Index idx = visible_model.surface_vert_pos.size() - 1;
        visible_model.surface_ibuffer.push_back ( idx );
    }

    size_t App::add_vertex ( Vector3f pos, Vector3f norm, Vector3f color ) {
        size_t i = visible_model.surface_vert_pos.size();
        visible_model.surface_vert_pos.push_back ( pos );
        visible_model.surface_vert_norm.push_back ( norm );
        visible_model.surface_vert_color.push_back ( color );
        return i;
    }

    void App::add_triangle ( size_t i1, size_t i2, size_t i3 ) {
        visible_model.surface_ibuffer.push_back ( i1 );
        visible_model.surface_ibuffer.push_back ( i2 );
        visible_model.surface_ibuffer.push_back ( i3 );
    }

    void App::add_quad ( size_t i1, size_t i2, size_t i3, size_t i4 ) {
        add_triangle ( i1, i2, i3 );
        add_triangle ( i3, i4, i1 );
    }

    void App::add_visible_face ( Dart& dart, float normal_sign ) {
        MeshNavigator nav = mesh->navigate ( dart );

        // Faces are normally shared between two hexas, but their data structure references only one of them, the 'main' (the first encountered when parsing the mesh file).
        // If the normal sign is -1, it means that the hexa we want to render is not the main.
        // Therefore a flip hexa is performed, along with a flip edge to maintain the winding.
        if ( normal_sign == -1 ) {
            nav = nav.flip_hexa().flip_edge();
        }

        // If hexa quality display is enabled, fetch the appropriate quality color.
        // Otherwise use the defautl coloration (white for outer faces, yellow for everything else)
        Vector3f color;

        if ( is_quality_color_mapping_enabled() ) {
            color = color_map.get ( mesh->normalized_hexa_quality[nav.hexa_index()] );
        } else {
            color = nav.is_face_boundary() ? this->default_outside_color : this->default_inside_color;
        }

        for ( int i = 0; i < 2; ++i ) {
            for ( int j = 0; j < 2; ++j ) {
                add_visible_wireframe ( nav.dart() );
                nav = nav.rotate_on_face();
            }
        }

        nav = mesh->navigate ( dart );

        if ( normal_sign == -1 ) {
            nav = nav.flip_vert();    // TODO flip hexa/edge instead? same thing?
        }

        Vert& vert = nav.vert();
        Index idx = visible_model.surface_vert_pos.size();

        do {
            add_vertex ( nav.vert().position, nav.face().normal * normal_sign, color );
            nav = nav.rotate_on_face();
        } while ( nav.vert() != vert );

        add_triangle ( idx + 0, idx + 1, idx + 2 );
        add_triangle ( idx + 2, idx + 3, idx + 0 );
    }

    void App::add_visible_wireframe ( Dart& dart ) {
        MeshNavigator nav = mesh->navigate ( dart );
        //if (nav.edge().mark != mesh->mark) {
        //            nav.edge().mark = mesh->mark;
        MeshNavigator edge_nav = nav;
        bool boundary_singularity = false;
        bool boundary_crease = false;

        if ( this->do_show_boundary_singularity ) {
            if ( nav.incident_face_on_edge_num() != 2 ) {
                boundary_singularity = true;
            }
        }

        // if (this->do_show_boundary_creases) {
        //     Face& begin = nav.face();
        //     bool prev_face_is_boundary = false;
        //     Vector3f prev_face_normal;
        //     do {
        //         if (!prev_face_is_boundary) {
        //             if (nav.is_face_boundary()) {
        //                 prev_face_is_boundary = true;
        //                 prev_face_normal = nav.face().normal;
        //             }
        //         } else {
        //             if (nav.is_face_boundary()) {
        //                 // float dot = nav.face().normal.dot(prev_face_normal);
        //                 // if (std::acos(std::abs(dot)) > 1 * D2R) {
        //                     boundary_crease = true;
        //                     break;      // Ends in a different dart from the starting one!
        //                 // }
        //             } else {
        //                 prev_face_is_boundary = false;
        //             }
        //         }
        //         nav = nav.rotate_on_edge();
        //     } while (nav.face() != begin);
        // }
        for ( int v = 0; v < 2; ++v ) {
            visible_model.wireframe_vert_pos.push_back ( edge_nav.vert().position );

            // if (this->do_show_boundary_singularity && boundary_singularity
            // && this->do_show_boundary_creases && boundary_crease) {
            // visible_model.wireframe_vert_color.push_back(Vector3f(0, 1, 1));
            // } else
            if ( this->do_show_boundary_singularity && boundary_singularity ) {
                visible_model.wireframe_vert_color.push_back ( Vector3f ( 0, 0, 1 ) );
                // } else if (this->do_show_boundary_creases && boundary_crease) {
                // visible_model.wireframe_vert_color.push_back(Vector3f(0, 1, 0));
            } else {
                visible_model.wireframe_vert_color.push_back ( Vector3f ( 0, 0, 0 ) );
            }

            edge_nav = edge_nav.flip_vert();
        }

        //}
    }

    void App::add_filtered_face ( Dart& dart ) {
        MeshNavigator nav = mesh->navigate ( dart );

        for ( int i = 0; i < 2; ++i ) {
            for ( int j = 0; j < 2; ++j ) {
                filtered_model.surface_vert_pos.push_back ( mesh->verts[nav.dart().vert].position );
                add_filtered_wireframe ( nav.dart() );
                nav = nav.rotate_on_face();
            }

            filtered_model.surface_vert_pos.push_back ( mesh->verts[nav.dart().vert].position );
            Vector3f normal = nav.face().normal;
            filtered_model.surface_vert_norm.push_back ( normal );
            filtered_model.surface_vert_norm.push_back ( normal );
            filtered_model.surface_vert_norm.push_back ( normal );
        }
    }

    void App::add_filtered_wireframe ( Dart& dart ) {
        MeshNavigator nav = mesh->navigate ( dart );
        //if (nav.edge().mark != mesh->mark) {
        //            nav.edge().mark = mesh->mark;
        MeshNavigator edge_nav = nav;

        for ( int v = 0; v < 2; ++v ) {
            filtered_model.wireframe_vert_pos.push_back ( mesh->verts[edge_nav.dart().vert].position );
            edge_nav = edge_nav.flip_vert();
        }

        //}
    }

    void App::prepare_geometry() {
        for ( size_t i = 0; i < mesh->faces.size(); ++i ) {
            MeshNavigator nav = mesh->navigate ( mesh->faces[i] );

            // hexa a visible, hexa b not existing or not visible
            if ( !mesh->is_marked ( nav.hexa() ) && ( nav.dart().hexa_neighbor == -1 || mesh->is_marked ( nav.flip_hexa().hexa() ) ) ) {
                this->add_visible_face ( nav.dart(), 1 );
                // hexa a invisible, hexa b existing and visible
            } else if ( mesh->is_marked ( nav.hexa() ) && nav.dart().hexa_neighbor != -1 && !mesh->is_marked ( nav.flip_hexa().hexa() ) ) {
                this->add_visible_face ( nav.dart(), -1 );
                // add_filtered_face(nav.dart());
                // face was culled by the plane, is surface
            } else if ( mesh->is_marked ( nav.hexa() ) && nav.dart().hexa_neighbor == -1 ) {
                this->add_filtered_face ( nav.dart() );
            }
        }
    }

    float gap = 0.3;

    void App::build_gap_hexa ( const Vector3f pp[8], const Vector3f nn[6], const bool vv[8], const Vector3f ww[6] ) {
        if ( !vv[0] && !vv[1] && !vv[2] && !vv[3] && !vv[4] && !vv[5] && !vv[6] && !vv[7] ) {
            return;
        }

        Vector3f bari ( 0, 0, 0 );

        for ( int i = 0; i < 8; i++ ) {
            bari += pp[i];
        }

        bari *= ( gap / 8 );
        auto addSide = [&] ( int v0, int v1, int v2, int v3, int fi ) {
            if ( vv[v0] || vv[v1] || vv[v2] || vv[v3] ) add_quad (
                    add_vertex ( ( !vv[v0] ) ? pp[v0] : ( pp[v0] * ( 1 - gap ) + bari ), nn[fi], ww[fi] ),
                    add_vertex ( ( !vv[v3] ) ? pp[v3] : ( pp[v3] * ( 1 - gap ) + bari ), nn[fi], ww[fi] ),
                    add_vertex ( ( !vv[v2] ) ? pp[v2] : ( pp[v2] * ( 1 - gap ) + bari ), nn[fi], ww[fi] ),
                    add_vertex ( ( !vv[v1] ) ? pp[v1] : ( pp[v1] * ( 1 - gap ) + bari ), nn[fi], ww[fi] )
                );
        };
        addSide ( 0 + 0, 1 + 0, 3 + 0, 2 + 0, 0 );
        addSide ( 1 + 4, 0 + 4, 2 + 4, 3 + 4, 1 );
        addSide ( 0 + 0, 4 + 0, 5 + 0, 1 + 0, 2 );
        addSide ( 4 + 2, 0 + 2, 1 + 2, 5 + 2, 3 );
        addSide ( 0 + 0, 2 + 0, 6 + 0, 4 + 0, 4 );
        addSide ( 2 + 1, 0 + 1, 4 + 1, 6 + 1, 5 );
    }

    void App::prepare_cracked_geometry() {
        auto mark_face_as_visible = [] ( Mesh * mesh, Dart & dart ) {
            MeshNavigator nav = mesh->navigate ( dart );
            Vert& vert = nav.vert();

            do {
                nav.vert().is_visible = true;
                nav = nav.rotate_on_face();
            } while ( nav.vert() != vert );
        };

        for ( size_t i = 0; i < mesh->faces.size(); ++i ) {
            MeshNavigator nav = mesh->navigate ( mesh->faces[i] );

            if ( !mesh->is_marked ( nav.hexa() ) && ( nav.dart().hexa_neighbor == -1 || mesh->is_marked ( nav.flip_hexa().hexa() ) ) ) {
                mark_face_as_visible ( this->mesh, nav.dart() );
            } else if ( mesh->is_marked ( nav.hexa() ) && nav.dart().hexa_neighbor != -1 && !mesh->is_marked ( nav.flip_hexa().hexa() ) ) {
                mark_face_as_visible ( this->mesh, nav.dart() );
            }
        }

        // TODO
        Vector3f colors[6];

        for ( size_t i = 0; i < 6; ++i ) {
            colors[i] = Vector3f ( 1, 0, 0 );
        }

        for ( size_t i = 0; i < mesh->hexas.size(); ++i ) {
            // ******
            //    P6------P7
            //   / |     / |
            //  P4------P5 |
            //  |  |    |  |
            //  | P2----|--P3
            //  | /     | /
            //  P0------P1
            Vector3f    verts_pos[8];
            bool        verts_vis[8];
            bool        faces_vis[6]; // true: external, false: internal
            Vector3f    faces_norms[6];
            // ******
            // Extract face normals
            MeshNavigator nav = this->mesh->navigate ( mesh->hexas[i] );
            Face& face = nav.face();

            for ( size_t f = 0; f < 6; ++f ) {
                MeshNavigator n2 = this->mesh->navigate ( nav.face() );
                float normal_sign;

                if ( n2.hexa() == nav.hexa() ) {
                    normal_sign = 1;
                } else {
                    normal_sign = -1;
                    n2 = n2.flip_hexa().flip_edge();
                }

                faces_norms[f] = n2.face().normal * normal_sign;
                faces_vis[f] = n2.dart().hexa_neighbor == -1;
                nav = nav.next_hexa_face();
            }

            // Extract vertices
            nav = mesh->navigate ( mesh->hexas[i] );
            auto store_vert = [&] ( size_t i ) {
                verts_pos[i] = nav.vert().position;
                verts_vis[i] = nav.vert().is_visible;
            };
            nav = mesh->navigate ( mesh->hexas[i] ).flip_vert();
            store_vert ( 0 );
            nav = mesh->navigate ( mesh->hexas[i] );
            store_vert ( 1 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_side().flip_vert();
            store_vert ( 2 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_side();
            store_vert ( 3 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_vert().flip_edge().flip_vert();
            store_vert ( 4 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_edge().flip_vert();
            store_vert ( 5 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_side().flip_vert().flip_edge().flip_vert();
            store_vert ( 6 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_side().flip_edge().flip_vert();
            store_vert ( 7 );
            build_gap_hexa ( verts_pos, faces_norms, verts_vis, colors );
        }
    }

    void App::prepare_smooth_geometry() {
        auto mark_face_as_visible = [] ( Mesh * mesh, Dart & dart ) {
            MeshNavigator nav = mesh->navigate ( dart );
            Vert& vert = nav.vert();

            do {
                nav.vert().is_visible = true;
                nav = nav.rotate_on_face();
            } while ( nav.vert() != vert );
        };

        for ( size_t i = 0; i < mesh->faces.size(); ++i ) {
            MeshNavigator nav = mesh->navigate ( mesh->faces[i] );

            if ( !mesh->is_marked ( nav.hexa() ) && ( nav.dart().hexa_neighbor == -1 || mesh->is_marked ( nav.flip_hexa().hexa() ) ) ) {
                mark_face_as_visible ( this->mesh, nav.dart() );
            } else if ( mesh->is_marked ( nav.hexa() ) && nav.dart().hexa_neighbor != -1 && !mesh->is_marked ( nav.flip_hexa().hexa() ) ) {
                mark_face_as_visible ( this->mesh, nav.dart() );
            }
        }

        // TODO
        Vector3f colors[6];

        for ( size_t i = 0; i < 6; ++i ) {
            colors[i] = Vector3f ( 1, 0, 0 );
        }

        for ( size_t i = 0; i < mesh->hexas.size(); ++i ) {
            // ******
            //    P6------P7
            //   / |     / |
            //  P4------P5 |
            //  |  |    |  |
            //  | P2----|--P3
            //  | /     | /
            //  P0------P1
            Vector3f    verts_pos[8];
            bool        verts_vis[8];
            bool        faces_vis[6]; // true: external, false: internal
            Vector3f    faces_norms[6];
            // ******
            // Extract face normals
            MeshNavigator nav = this->mesh->navigate ( mesh->hexas[i] );
            Face& face = nav.face();

            for ( size_t f = 0; f < 6; ++f ) {
                MeshNavigator n2 = this->mesh->navigate ( nav.face() );
                float normal_sign;

                if ( n2.hexa() == nav.hexa() ) {
                    normal_sign = 1;
                } else {
                    normal_sign = -1;
                    n2 = n2.flip_hexa().flip_edge();
                }

                faces_norms[f] = n2.face().normal * normal_sign;
                faces_vis[f] = n2.dart().hexa_neighbor == -1;
                nav = nav.next_hexa_face();
            }

            // Extract vertices
            nav = mesh->navigate ( mesh->hexas[i] );
            auto store_vert = [&] ( size_t i ) {
                verts_pos[i] = nav.vert().position;
                verts_vis[i] = nav.vert().is_visible;
            };
            nav = mesh->navigate ( mesh->hexas[i] ).flip_vert();
            store_vert ( 0 );
            nav = mesh->navigate ( mesh->hexas[i] );
            store_vert ( 1 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_side().flip_vert();
            store_vert ( 2 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_side();
            store_vert ( 3 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_vert().flip_edge().flip_vert();
            store_vert ( 4 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_edge().flip_vert();
            store_vert ( 5 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_side().flip_vert().flip_edge().flip_vert();
            store_vert ( 6 );
            nav = mesh->navigate ( mesh->hexas[i] ).flip_side().flip_edge().flip_vert();
            store_vert ( 7 );
            build_gap_hexa ( verts_pos, faces_norms, verts_vis, colors );
        }
    }

    void App::build_surface_models() {
        if ( mesh == nullptr ) {
            return;
        }

        auto t_start = sample_time();
        mesh->unmark_all();
        visible_model.clear();
        filtered_model.clear();

        for ( size_t i = 0; i < filters.size(); ++i ) {
            filters[i]->filter ( *mesh );
        }

        this->prepare_smooth_geometry();
    }
}
