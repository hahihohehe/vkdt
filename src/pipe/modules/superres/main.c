#include "modules/api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void modify_roi_in(
        dt_graph_t *graph,
        dt_module_t *module)
{
    dt_roi_t *ri = &module->connector[0].roi;

    ri->wd = ri->full_wd;
    ri->ht = ri->full_ht;
    ri->scale = 1.0f;
}

void modify_roi_out(
        dt_graph_t *graph,
        dt_module_t *module)
{
    dt_roi_t *ri = &module->connector[0].roi;
    dt_roi_t *ro = &module->connector[1].roi;

    // always give full size and negotiate half or not in modify_roi_in
    ro->full_wd = ri->full_wd;
    ro->full_ht = ri->full_ht;
}

void
create_nodes(
        dt_graph_t  *graph,
        dt_module_t *module) {
    assert(graph->num_nodes < graph->max_nodes);
    const int id_gs = graph->num_nodes++;
    dt_node_t *node_gs = graph->node + id_gs;
    *node_gs = (dt_node_t) {
            .name   = dt_token("superres"),
            .kernel = dt_token("gs"),
            .module = module,
            .wd     = module->connector[0].roi.wd,
            .ht     = module->connector[0].roi.ht,
            .dp     = 1,
            .num_connectors = 2,
            .connector = {{
                                  .name   = dt_token("input"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rggb"),
                                  .format = dt_token("*"),
                                  .roi    = module->connector[0].roi,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("test"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[0].roi,
                          }},
            .push_constant_size = sizeof(uint32_t),
            .push_constant = {
                    module->img_param.filters,
            },
    };

    // connect input
    dt_connector_copy(graph, module, 0, id_gs, 0);

    if (module->connector[1].roi.scale == 1.0) {
        // no resampling needed

        // connect output
        dt_connector_copy(graph, module, 1, id_gs, 1);
    }
    else { // add resample node to graph, copy its output instead:
        assert(graph->num_nodes < graph->max_nodes);
        const int id_resample = graph->num_nodes++;
        graph->node[id_resample] = (dt_node_t) {
                .name   = dt_token("shared"),
                .kernel = dt_token("resample"),
                .module = module,
                .wd     = module->connector[1].roi.wd,
                .ht     = module->connector[1].roi.ht,
                .dp     = 1,
                .num_connectors = 2,
                .connector = {
                        {
                                .name   = dt_token("input"),
                                .type   = dt_token("read"),
                                .chan   = dt_token("rgba"),
                                .format = dt_token("f16"),
                                .roi    = module->connector[0].roi,
                                .connected_mi = -1,
                        },
                        {
                                .name   = dt_token("test"),
                                .type   = dt_token("write"),
                                .chan   = dt_token("rgba"),
                                .format = dt_token("f16"),
                                .roi    = module->connector[1].roi,
                        }
                },
        };
        CONN(dt_node_connect(graph, id_gs, 1, id_resample, 0));
        dt_connector_copy(graph, module, 1, id_resample, 1);
    }
}
