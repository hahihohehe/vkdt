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
    dt_roi_t *ro = &module->connector[10].roi;

    // always give full size and negotiate half or not in modify_roi_in
    ro->full_wd = ri->full_wd;
    ro->full_ht = ri->full_ht;
}

void
create_nodes(
        dt_graph_t  *graph,
        dt_module_t *module) {

    assert(graph->num_nodes < graph->max_nodes);
    const int id_cf = graph->num_nodes++;
    dt_node_t *node_cf = graph->node + id_cf;
    *node_cf = (dt_node_t) {
            .name   = dt_token("superres"),
            .kernel = dt_token("cf"),
            .module = module,
            .wd     = module->connector[0].roi.wd,
            .ht     = module->connector[0].roi.ht,
            .dp     = 1,
            .num_connectors = 4,
            .connector = {{
                                  .name   = dt_token("img"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rggb"),
                                  .format = dt_token("*"),
                                  .roi    = module->connector[0].roi,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("grad"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rg"),
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[0].roi,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("acc"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[0].roi,
                          },
                          {
                                  .name   = dt_token("cont"),
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
    dt_connector_copy(graph, module, 0, id_cf, 0);

    int num_connected = 0;
    int id_combine_prev;
    for (int i = 0; i < 3; i++)     // go through each input set
    {
        // check if input is connected
        if(module->connector[1+3*i].connected_mi >= 0 &&
           module->connector[1+3*i].connected_mc >= 0)
        {
            const int id_combine = graph->num_nodes++;
            dt_node_t *node_combine = graph->node + id_combine;
            *node_combine = (dt_node_t) {
                    .name   = dt_token("superres"),
                    .kernel = dt_token("combine"),
                    .module = module,
                    .wd     = module->connector[0].roi.wd,
                    .ht     = module->connector[0].roi.ht,
                    .dp     = 1,
                    .num_connectors = 8,
                    .connector = {{
                                          .name   = dt_token("img"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("rggb"),
                                          .format = dt_token("*"),
                                          .roi    = module->connector[0].roi,
                                          .connected_mi = -1,
                                  },
                                  {
                                          .name   = dt_token("off"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("rg"),
                                          .format = dt_token("f16"),
                                          .roi    = module->connector[0].roi,
                                          .connected_mi = -1,
                                  },{
                                          .name   = dt_token("mask"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("y"),
                                          .format = dt_token("f16"),
                                          .roi    = module->connector[0].roi,
                                          .connected_mi = -1,
                                  },{
                                          .name   = dt_token("acc_in"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("rgba"),
                                          .format = dt_token("f16"),
                                          .roi    = module->connector[0].roi,
                                          .connected_mi = -1,
                                  },{
                                          .name   = dt_token("cont_in"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("rgba"),
                                          .format = dt_token("f16"),
                                          .roi    = module->connector[0].roi,
                                          .connected_mi = -1,
                                  },
                                  {
                                          .name   = dt_token("grad"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("rg"),
                                          .format = dt_token("f16"),
                                          .roi    = module->connector[0].roi,
                                          .connected_mi = -1,
                                  },
                                  {
                                          .name   = dt_token("acc"),
                                          .type   = dt_token("write"),
                                          .chan   = dt_token("rgba"),
                                          .format = dt_token("f16"),
                                          .roi    = module->connector[0].roi,
                                  },
                                  {
                                          .name   = dt_token("cont"),
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

            // connect to previous node
            if (num_connected == 0)
            {       // this is the first combine node after cf
                CONN(dt_node_connect(graph, id_cf, 2, id_combine, 3));     // acc
                CONN(dt_node_connect(graph, id_cf, 3, id_combine, 4));     // cont
            }
            else
            {
                CONN(dt_node_connect(graph, id_combine_prev, 6, id_combine, 3));     // acc
                CONN(dt_node_connect(graph, id_combine_prev, 7, id_combine, 4));     // cont
            }
            // connect align inputs
            dt_connector_copy(graph, module, 1+3*i, id_combine, 0);     // img
            dt_connector_copy(graph, module, 1+3*i+1, id_combine, 1);   // mv
            dt_connector_copy(graph, module, 1+3*i+2, id_combine, 2);   // mask

            num_connected++;
            id_combine_prev = id_combine;
        }
        else
        {
            // ignore inputs that are not connected
        }
    }

    assert(graph->num_nodes < graph->max_nodes);
    const int id_norm = graph->num_nodes++;
    dt_node_t *node_norm = graph->node + id_norm;
    *node_norm = (dt_node_t) {
            .name   = dt_token("superres"),
            .kernel = dt_token("norm"),
            .module = module,
            .wd     = module->connector[0].roi.wd,
            .ht     = module->connector[0].roi.ht,
            .dp     = 1,
            .num_connectors = 3,
            .connector = {{
                                  .name   = dt_token("acc"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[0].roi,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("cont"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[0].roi,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("output"),
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
    CONN(dt_node_connect(graph, id_combine_prev, 6, id_norm, 0));     // acc
    CONN(dt_node_connect(graph, id_combine_prev, 7, id_norm, 1));     // cont

    if (module->connector[1].roi.scale == 1.0) {
        // no resampling needed

        // connect output
        dt_connector_copy(graph, module, 10, id_norm, 2);
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
        CONN(dt_node_connect(graph, id_norm, 2, id_resample, 0));
        dt_connector_copy(graph, module, 10, id_resample, 1);
    }
}
