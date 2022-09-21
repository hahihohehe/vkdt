#include "modules/api.h"
#include "config.h"

dt_graph_run_t
check_params(
        dt_module_t *module,
        uint32_t parid,
        void *oldval) {
    return s_graph_run_all;
}

// the roi callbacks are only needed for the debug outputs. other than that
// the default implementation would work fine for us.
void modify_roi_in(
        dt_graph_t *graph,
        dt_module_t *module) {
    /*module->connector[4].roi.wd = module->connector[4].roi.full_wd;
    module->connector[4].roi.ht = module->connector[4].roi.full_ht;
    module->connector[4].roi.scale = 1.0f;
    module->connector[6].roi.wd = module->connector[6].roi.full_wd;
    module->connector[6].roi.ht = module->connector[6].roi.full_ht;
    module->connector[6].roi.scale = 1.0f;*/
    module->connector[0].roi = module->connector[1].roi;
    module->connector[2].roi = module->connector[1].roi;
    module->connector[3].roi = module->connector[1].roi;
    module->connector[4].roi = module->connector[1].roi;
}

void modify_roi_out(
        dt_graph_t *graph,
        dt_module_t *module) {
    module->connector[1].roi = module->connector[0].roi;
    module->connector[5].roi = module->connector[0].roi;
}

// input connectors: fixed raw file
//                   to-be-warped raw file
// output connector: warped raw file
void
create_nodes(
        dt_graph_t *graph,
        dt_module_t *module) {
    int lk_r = dt_module_param_int(module, dt_module_get_param(module->so, dt_token("rounds")))[0];

    dt_roi_t roi_in = module->connector[0].roi;
    const int block = module->img_param.filters == 9u ? 3 : (module->img_param.filters == 0 ? 1 : 2);
    // const int block = 2;
    printf("tlk block size: %i\n", block);

    dt_roi_t roi_block = roi_in;
    if (block != 1)
    {
        roi_block.full_wd += block - 1;
        roi_block.full_wd /= block;
        roi_block.full_ht += block - 1;
        roi_block.full_ht /= block;
        roi_block.wd += block - 1;
        roi_block.wd /= block;
        roi_block.ht += block - 1;
        roi_block.ht /= block;
    }

    // a tile is e.g. a 16px x 16px block of raw input,
    // so half of that in each dimension on half resolution image
    dt_roi_t roi_tile = roi_in;
    roi_tile.full_wd += TILE_SIZE - 1;
    roi_tile.full_wd /= TILE_SIZE;
    roi_tile.full_ht += TILE_SIZE - 1;
    roi_tile.full_ht /= TILE_SIZE;
    roi_tile.wd += TILE_SIZE - 1;
    roi_tile.wd /= TILE_SIZE;
    roi_tile.ht += TILE_SIZE - 1;
    roi_tile.ht /= TILE_SIZE;

    // ========================================================================
    // ========= create grayscale images ======================================
    // ========================================================================
    int id_half[2] = {0};
    const dt_image_params_t *img_param = dt_module_get_input_img_param(graph, module, dt_token("input"));
    if(!img_param) return;
    uint32_t *blacki = (uint32_t *)img_param->black;
    uint32_t *whitei = (uint32_t *)img_param->white;
    for(int k=0;k<2;k++) {
        assert(graph->num_nodes < graph->max_nodes);
        id_half[k] = graph->num_nodes++;
        graph->node[id_half[k]] = (dt_node_t) {
                .name   = dt_token("tlk"),
                .kernel = dt_token("half"),
                .module = module,
                .wd     = roi_block.wd,
                .ht     = roi_block.ht,
                .dp     = 1,
                .num_connectors = 2,
                .connector = {{
                                      .name   = dt_token("input"),
                                      .type   = dt_token("read"),
                                      .chan   = module->img_param.filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                                      .format = module->connector[k].format,
                                      .roi    = roi_in,
                                      .connected_mi = -1,
                              },
                              {
                                      .name   = dt_token("output"),
                                      .type   = dt_token("write"),
                                      .chan   = dt_token("y"),
                                      .format = dt_token("f16"),
                                      .roi    = roi_block,
                              }},
                .push_constant_size = 9 * sizeof(uint32_t),
                .push_constant = {blacki[0], blacki[1], blacki[2], blacki[3],
                                  whitei[0], whitei[1], whitei[2], whitei[3],
                                  img_param->filters},
        };
        // these are the "alignsrc" and "aligndst" channels:
        dt_connector_copy(graph, module, 2+k, id_half[k], 0);
    }

    // ========================================================================
    // ========= rescale input offsets (img size -> block size) ===============
    // ========================================================================
    // currently only for rgb and bayer input data
    assert(graph->num_nodes < graph->max_nodes);
    int id_mv = graph->num_nodes++;
    graph->node[id_mv] = (dt_node_t) {
            .name   = dt_token("tlk"),
            .kernel = dt_token("mvprep"),
            .module = module,
            .wd     = roi_tile.wd,
            .ht     = roi_tile.ht,
            .dp     = 1,
            .num_connectors = 2,
            .connector = {{
                                  .name   = dt_token("input"),
                                  .type   = dt_token("read"),
                                  .chan   = module->connector[4].connected_mi > 0 ? dt_token("rg") : block > 1 ? dt_token("rggb") : dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = roi_in,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("output"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rg"),
                                  .format = dt_token("f16"),
                                  .roi    = roi_tile,
                          }},
            .push_constant_size = 2 * sizeof(uint32_t),
            .push_constant = {block, module->connector[4].connected_mi},
    };

    if (module->connector[4].connected_mi > 0)
    {
        dt_connector_copy(graph, module, 4, id_mv, 0);
        printf("mv_in connected\n");
    }
    else
    {
        dt_connector_copy(graph, module, 0, id_mv, 0);
        printf("mv_in disconnected\n");
    }



    // ========================================================================
    // ========= run lucas kanade =============================================
    // ========================================================================
    int last_lk;
    for (int i = 0; i < lk_r; i++) {
        assert(graph->num_nodes < graph->max_nodes);
        const int id_lk = graph->num_nodes++;
        graph->node[id_lk] = (dt_node_t) {
                .name   = dt_token("tlk"),
                .kernel = dt_token("lk"),
                .module = module,
                .wd     = roi_tile.wd,
                .ht     = roi_tile.ht,
                .dp     = 1,
                .num_connectors = 4,
                .connector = {{
                                      .name   = dt_token("F"),
                                      .type   = dt_token("read"),
                                      .chan   = dt_token("y"),
                                      .format = dt_token("f16"),
                                      .roi    = roi_block,
                                      .connected_mi = -1,
                              },
                              {
                                      .name   = dt_token("G"),
                                      .type   = dt_token("read"),
                                      .chan   = dt_token("y"),
                                      .format = dt_token("f16"),
                                      .roi    = roi_block,
                                      .connected_mi = -1,
                              },
                              {
                                      .name   = dt_token("off"),
                                      .type   = dt_token("read"),
                                      .chan   = dt_token("rg"),
                                      .format = dt_token("f16"),
                                      .roi    = roi_tile,
                                      .connected_mi = -1,
                              },
                              {
                                      .name   = dt_token("output"),
                                      .type   = dt_token("write"),
                                      .chan   = dt_token("rg"),
                                      .format = dt_token("f16"),
                                      .roi    = roi_tile,
                              }},
        };

        // connect F and G
        CONN(dt_node_connect(graph, id_half[0], 1, id_lk, 0));
        CONN(dt_node_connect(graph, id_half[1], 1, id_lk, 1));

        // connect off
        if (i > 0)
        {
            CONN(dt_node_connect(graph, last_lk, 3, id_lk, 2));
        }
        else
        {
            CONN(dt_node_connect(graph, id_mv, 1, id_lk, 2));
        }

        last_lk = id_lk;
    }

    // ========================================================================
    // ========= create output, visn and interpolate mv =======================
    // ========================================================================
    assert(graph->num_nodes < graph->max_nodes);
    const int id_warp = graph->num_nodes++;
    graph->node[id_warp] = (dt_node_t) {
            .name   = dt_token("tlk"),
            .kernel = dt_token("warp"),
            .module = module,
            .wd     = roi_in.wd,
            .ht     = roi_in.ht,
            .dp     = 1,
            .num_connectors = 5,
            .connector = {{
                                  .name   = dt_token("input"),
                                  .type   = dt_token("read"),
                                  .chan   = module->img_param.filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                                  .format = dt_token("f16"),
                                  .roi    = roi_in,
                                  .connected_mi = -1,
                          },{
                                  .name   = dt_token("offset"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rg"),
                                  .format = dt_token("f16"),
                                  .roi    = roi_tile,
                                  .connected_mi = -1,
                          },{
                                  .name   = dt_token("output"),
                                  .type   = dt_token("write"),
                                  .chan   = module->img_param.filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                                  .format = dt_token("f16"),
                                  .roi    = roi_in,
                          },{
                                  .name   = dt_token("visn"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = roi_in,
                          },{
                                  .name   = dt_token("mv"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rg"),
                                  .format = dt_token("f16"),
                                  .roi    = roi_in,
                          }},
            .push_constant_size = 2*sizeof(uint32_t),
            .push_constant = {
                    module->img_param.filters,
                    block,
            },
    };

    dt_connector_copy(graph, module, 0, id_warp, 0);    // input
    //dt_connector_copy(graph, module, 1, id_warp, 3);    // visn
    dt_connector_copy(graph, module, 1, id_warp, 2);    // warped output
    dt_connector_copy(graph, module, 5, id_warp, 4);    // mv out

    if (lk_r == 0)
    {
        CONN(dt_node_connect(graph, id_mv, 1, id_warp, 1));
    } else {
        CONN(dt_node_connect(graph, last_lk, 3, id_warp, 1));
    }
}
