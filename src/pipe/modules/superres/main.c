#include "modules/api.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define OUTPUT 31

void modify_roi_in(
        dt_graph_t *graph,
        dt_module_t *module)
{
    dt_roi_t *ri = &module->connector[0].roi;

    // full volle output
    // wd: was ich rendern möchconnectorte
    // scale: factor zw. den beiden
    ri->wd = ri->full_wd;
    ri->ht = ri->full_ht;
    ri->scale = 1.0f;

    // weitere inputs

    for (int i = 1; i < OUTPUT; ++i)
    {
        module->connector[i].roi.wd = module->connector[i].roi.full_wd;
        module->connector[i].roi.ht = module->connector[i].roi.full_ht;
        module->connector[i].roi.scale = 1.0f;
    }


}

void modify_roi_out(
        dt_graph_t *graph,
        dt_module_t *module)
{
    dt_roi_t *ri = &module->connector[0].roi;
    dt_roi_t *ro = &module->connector[OUTPUT].roi;

    // get size specified in params
    float res = dt_module_param_float(module, dt_module_get_param(module->so, dt_token("inc")))[0];
    res += 1.0f;
    ro->full_ht = res * (float) ri->full_ht;
    ro->full_wd = res * (float) ri->full_wd;

    module->connector[OUTPUT+1].roi.full_wd = (ri->full_wd + 1) / 2;
    module->connector[OUTPUT+1].roi.full_ht = (ri->full_ht + 1) / 2;

    module->img_param.filters = 0u;     // no more mosaic after this module
}

dt_graph_run_t
check_params(
        dt_module_t *module,
        uint32_t     parid,
        void        *oldval)
{
    return s_graph_run_all; // maybe that fixes the redrawing bug?
}

void
create_nodes(
        dt_graph_t  *graph,
        dt_module_t *module) {
    dt_roi_t *ri = &module->connector[0].roi;
    dt_roi_t *ro = &module->connector[OUTPUT].roi;
    const dt_image_params_t* img_params = dt_module_get_input_img_param(graph, module, dt_token("input"));

    int id_grad[11] = { 0 };

    assert(graph->num_nodes < graph->max_nodes);
    id_grad[0] = graph->num_nodes++;
    dt_node_t *node_grad = graph->node + id_grad[0];
    *node_grad = (dt_node_t) {
        .name   = dt_token("superres"),
        .kernel = dt_token("grad"),
        .module = module,
        .wd     = ri->wd,
        .ht     = ri->ht,
        .dp     = 1,
        .num_connectors = 2,
        .connector = {{
                          .name   = dt_token("img"),
                          .type   = dt_token("read"),
                          .chan   = dt_token("rggb"),
                          .format = dt_token("*"),
                          .roi    = *ri,
                          .connected_mi = -1,
                      },
                      {
                          .name   = dt_token("grad"),
                          .type   = dt_token("write"),
                          .chan   = dt_token("rg"),
                          .format = dt_token("f16"),
                          .roi    = *ri,
                      }},
    };
    dt_connector_copy(graph, module, 0, id_grad[0], 0);


    assert(graph->num_nodes < graph->max_nodes);
    const int id_cf = graph->num_nodes++;
    dt_node_t *node_cf = graph->node + id_cf;
    *node_cf = (dt_node_t) {
            .name   = dt_token("superres"),
            .kernel = dt_token("cf_g"),
            .module = module,
            .wd     = ro->wd,
            .ht     = ro->ht,
            .dp     = 1,
            .num_connectors = 4,
            .connector = {{
                                  .name   = dt_token("img"),
                                  .type   = dt_token("read"),
                                  .chan   = img_params->filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                                  .format = dt_token("*"),
                                  .roi    = *ri,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("grad"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rg"),
                                  .format = dt_token("f16"),
                                  .roi    = *ri,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("acc"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = *ro,
                          },
                          {
                                  .name   = dt_token("cont"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = *ro,
                          }},
            .push_constant_size = sizeof(uint32_t),
            .push_constant = {
                    img_params->filters,
            },
    };
    dt_connector_copy(graph, module, 0, id_cf, 0);
    CONN(dt_node_connect(graph, id_grad[0], 1, id_cf, 1));     // grad

    int num_connected = 0;
    int id_combine_prev;
    for (int i = 0; i < 10; i++)     // go through each input set
    {
        // check if input is connected
        if(module->connector[1+3*i].connected_mi >= 0 &&
           module->connector[1+3*i].connected_mc >= 0)
        {
#ifndef DT_SR_REF_GRAD
            assert(graph->num_nodes < graph->max_nodes);
            id_grad[i+1] = graph->num_nodes++;
            dt_node_t *node_grad = graph->node + id_grad[i+1];
            *node_grad = (dt_node_t) {
              .name   = dt_token("superres"),
              .kernel = dt_token("grad"),
              .module = module,
              .wd     = ri->wd,
              .ht     = ri->ht,
              .dp     = 1,
              .num_connectors = 2,
              .connector = {{
                                .name   = dt_token("img"),
                                .type   = dt_token("read"),
                                .chan   = img_params->filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                                .format = dt_token("*"),
                                .roi    = *ri,
                                .connected_mi = -1,
                            },
                            {
                                .name   = dt_token("grad"),
                                .type   = dt_token("write"),
                                .chan   = dt_token("rg"),
                                .format = dt_token("f16"),
                                .roi    = *ri,
                            }},
            };
            dt_connector_copy(graph, module, 1+3*i, id_grad[i+1], 0);
#endif

            assert(graph->num_nodes < graph->max_nodes);
            const int id_combine = graph->num_nodes++;
            dt_node_t *node_combine = graph->node + id_combine;
            *node_combine = (dt_node_t) {
                    .name   = dt_token("superres"),
                    .kernel = dt_token("comb_g"),
                    .module = module,
                    .wd     = ro->wd,
                    .ht     = ro->ht,
                    .dp     = 1,
                    .num_connectors = 8,
                    .connector = {{
                                          .name   = dt_token("img"),
                                          .type   = dt_token("read"),
                                          .chan   = img_params->filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                                          .format = dt_token("*"),
                                          .roi    = *ri,
                                          .connected_mi = -1,
                                  },
                                  {
                                          .name   = dt_token("off"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("rg"),
                                          .format = dt_token("f16"),
                                          .roi    = *ri,
                                          .connected_mi = -1,
                                  },{
                                          .name   = dt_token("mask"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("y"),
                                          .format = dt_token("f16"),
                                          .roi    = *ri,
                                          .connected_mi = -1,
                                  },{
                                          .name   = dt_token("acc_in"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("rgba"),
                                          .format = dt_token("f16"),
                                          .roi    = *ro,
                                          .connected_mi = -1,
                                  },{
                                          .name   = dt_token("cont_in"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("rgba"),
                                          .format = dt_token("f16"),
                                          .roi    = *ro,
                                          .connected_mi = -1,
                                  },
                                  {
                                          .name   = dt_token("grad"),
                                          .type   = dt_token("read"),
                                          .chan   = dt_token("rg"),
                                          .format = dt_token("f16"),
                                          .roi    = *ri,
                                          .connected_mi = -1,
                                  },
                                  {
                                          .name   = dt_token("acc"),
                                          .type   = dt_token("write"),
                                          .chan   = dt_token("rgba"),
                                          .format = dt_token("f16"),
                                          .roi    = *ro,
                                  },
                                  {
                                          .name   = dt_token("cont"),
                                          .type   = dt_token("write"),
                                          .chan   = dt_token("rgba"),
                                          .format = dt_token("f16"),
                                          .roi    = *ro,
                                  }},
                    .push_constant_size = 2 * sizeof(uint32_t),
                    .push_constant = {
                            img_params->filters,
                            (uint32_t) i + 1,
                    },
            };

            // connect grad
            CONN(dt_node_connect(graph, id_grad[i+1], 1, id_combine, 5));     // grad
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

    // combine kernels for red and blue channel
    // separate pass is needed because green channel needs to be known here
    int id_cf_rb;
    if (img_params->filters != 0)
    {
        assert(graph->num_nodes < graph->max_nodes);
        const int id_norm_g = graph->num_nodes++;
        dt_node_t *node_norm_g = graph->node + id_norm_g;
        *node_norm_g = (dt_node_t) {
                .name   = dt_token("superres"),
                .kernel = dt_token("norm_g"),
                .module = module,
                .wd     = ro->wd,
                .ht     = ro->ht,
                .dp     = 1,
                .num_connectors = 3,
                .connector = {{
                                      .name   = dt_token("acc"),
                                      .type   = dt_token("read"),
                                      .chan   = dt_token("rgba"),
                                      .format = dt_token("f16"),
                                      .roi    = *ro,
                                      .connected_mi = -1,
                              },
                              {
                                      .name   = dt_token("cont"),
                                      .type   = dt_token("read"),
                                      .chan   = dt_token("rgba"),
                                      .format = dt_token("f16"),
                                      .roi    = *ro,
                                      .connected_mi = -1,
                              },
                              {
                                      .name   = dt_token("output"),
                                      .type   = dt_token("write"),
                                      .chan   = dt_token("rgba"),
                                      .format = dt_token("f16"),
                                      .roi    = *ro,
                              }},
                .push_constant_size = sizeof(uint32_t),
                .push_constant = {
                        img_params->filters,
                },
        };
        if (num_connected == 0)
        {   // only reference image connected
            CONN(dt_node_connect(graph, id_cf, 2, id_norm_g, 0));     // acc
            CONN(dt_node_connect(graph, id_cf, 3, id_norm_g, 1));     // cont
        }
        else
        {
            CONN(dt_node_connect(graph, id_combine_prev, 6, id_norm_g, 0));     // acc
            CONN(dt_node_connect(graph, id_combine_prev, 7, id_norm_g, 1));     // cont
        }

        assert(graph->num_nodes < graph->max_nodes);
        id_cf_rb = graph->num_nodes++;
        dt_node_t *node_cf_rb = graph->node + id_cf_rb;
        *node_cf_rb = (dt_node_t) {
                .name   = dt_token("superres"),
                .kernel = dt_token("cf_rb"),
                .module = module,
                .wd     = ro->wd,
                .ht     = ro->ht,
                .dp     = 1,
                .num_connectors = 5,
                .connector = {{
                                      .name   = dt_token("img"),
                                      .type   = dt_token("read"),
                                      .chan   = img_params->filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                                      .format = dt_token("*"),
                                      .roi    = *ri,
                                      .connected_mi = -1,
                              },
                              {
                                      .name   = dt_token("grad"),
                                      .type   = dt_token("read"),
                                      .chan   = dt_token("rg"),
                                      .format = dt_token("f16"),
                                      .roi    = *ri,
                                      .connected_mi = -1,
                              },
                              {
                                      .name   = dt_token("acc_in"),
                                      .type   = dt_token("read"),
                                      .chan   = dt_token("rgba"),
                                      .format = dt_token("f16"),
                                      .roi    = *ro,
                                      .connected_mi = -1,
                              },
                              {
                                      .name   = dt_token("acc_out"),
                                      .type   = dt_token("write"),
                                      .chan   = dt_token("rgba"),
                                      .format = dt_token("f16"),
                                      .roi    = *ro,
                              },
                              {
                                      .name   = dt_token("cont_out"),
                                      .type   = dt_token("write"),
                                      .chan   = dt_token("rgba"),
                                      .format = dt_token("f16"),
                                      .roi    = *ro,
                              }},
                .push_constant_size = sizeof(uint32_t),
                .push_constant = {
                        img_params->filters,
                },
        };
        dt_connector_copy(graph, module, 0, id_cf_rb, 0);
        CONN(dt_node_connect(graph, id_grad[0], 1, id_cf_rb, 1));     // grad
        CONN(dt_node_connect(graph, id_norm_g, 2, id_cf_rb, 2));     // acc


        int connection_counter = 0;
        for (int i = 0; i < 10; i++)     // go through each input set
        {
            // check if input is connected
            if(module->connector[1+3*i].connected_mi >= 0 &&
               module->connector[1+3*i].connected_mc >= 0)
            {
                assert(graph->num_nodes < graph->max_nodes);
                const int id_combine = graph->num_nodes++;
                dt_node_t *node_combine = graph->node + id_combine;
                *node_combine = (dt_node_t) {
                        .name   = dt_token("superres"),
                        .kernel = dt_token("comb_rb"),
                        .module = module,
                        .wd     = ro->wd,
                        .ht     = ro->ht,
                        .dp     = 1,
                        .num_connectors = 8,
                        .connector = {{
                                              .name   = dt_token("img"),
                                              .type   = dt_token("read"),
                                              .chan   = img_params->filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                                              .format = dt_token("*"),
                                              .roi    = *ri,
                                              .connected_mi = -1,
                                      },
                                      {
                                              .name   = dt_token("off"),
                                              .type   = dt_token("read"),
                                              .chan   = dt_token("rg"),
                                              .format = dt_token("f16"),
                                              .roi    = *ri,
                                              .connected_mi = -1,
                                      },{
                                              .name   = dt_token("mask"),
                                              .type   = dt_token("read"),
                                              .chan   = dt_token("y"),
                                              .format = dt_token("f16"),
                                              .roi    = *ri,
                                              .connected_mi = -1,
                                      },{
                                              .name   = dt_token("acc_in"),
                                              .type   = dt_token("read"),
                                              .chan   = dt_token("rgba"),
                                              .format = dt_token("f16"),
                                              .roi    = *ro,
                                              .connected_mi = -1,
                                      },{
                                              .name   = dt_token("cont_in"),
                                              .type   = dt_token("read"),
                                              .chan   = dt_token("rgba"),
                                              .format = dt_token("f16"),
                                              .roi    = *ro,
                                              .connected_mi = -1,
                                      },
                                {
                                              .name   = dt_token("grad"),
                                              .type   = dt_token("read"),
                                              .chan   = dt_token("rg"),
                                              .format = dt_token("f16"),
                                              .roi    = *ri,
                                              .connected_mi = -1,
                                      },
                                {
                                              .name   = dt_token("acc"),
                                              .type   = dt_token("write"),
                                              .chan   = dt_token("rgba"),
                                              .format = dt_token("f16"),
                                              .roi    = *ro,
                                      },
                                {
                                              .name   = dt_token("cont"),
                                              .type   = dt_token("write"),
                                              .chan   = dt_token("rgba"),
                                              .format = dt_token("f16"),
                                              .roi    = *ro,
                                      }},
                        .push_constant_size = 2 * sizeof(uint32_t),
                        .push_constant = {
                                img_params->filters,
                                (uint32_t) i + 1,
                        },
                };

                // connect grad
                CONN(dt_node_connect(graph, id_grad[i+1], 1, id_combine, 5));     // grad
                // connect to previous node
                if (connection_counter == 0)
                {       // this is the first combine node after cf
                    CONN(dt_node_connect(graph, id_cf_rb, 3, id_combine, 3));     // acc
                    CONN(dt_node_connect(graph, id_cf_rb, 4, id_combine, 4));     // cont
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

                connection_counter++;
                id_combine_prev = id_combine;
            }
            else
            {
                // ignore inputs that are not connected
            }
        }

    }

    assert(graph->num_nodes < graph->max_nodes);
    const int id_norm = graph->num_nodes++;
    dt_node_t *node_norm = graph->node + id_norm;
    *node_norm = (dt_node_t) {
            .name   = dt_token("superres"),
            .kernel = dt_token("norm_rb"),
            .module = module,
            .wd     = ro->wd,
            .ht     = ro->ht,
            .dp     = 1,
            .num_connectors = 3,
            .connector = {{
                                  .name   = dt_token("acc"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = *ro,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("cont"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = *ro,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("output"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = *ro,
                          }},
            .push_constant_size = sizeof(uint32_t),
            .push_constant = {
                    img_params->filters,
            },
    };
    if (num_connected == 0)
    {   // only reference image connected
        CONN(dt_node_connect(graph, id_cf_rb, 3, id_norm, 0));     // acc
        CONN(dt_node_connect(graph, id_cf_rb, 4, id_norm, 1));     // cont
    }
    else
    {
        CONN(dt_node_connect(graph, id_combine_prev, 6, id_norm, 0));     // acc
        CONN(dt_node_connect(graph, id_combine_prev, 7, id_norm, 1));     // cont
    }

    // connect debug output
    assert(graph->num_nodes < graph->max_nodes);
    int id_visn = graph->num_nodes++;
    dt_node_t *node_visn = graph->node + id_visn;
    *node_visn = (dt_node_t) {
            .name   = dt_token("superres"),
            .kernel = dt_token("visn"),
            .module = module,
            .wd     = module->connector[OUTPUT+1].roi.wd,
            .ht     = module->connector[OUTPUT+1].roi.ht,
            .dp     = 1,
            .num_connectors = 2,
            .connector = {{
                                  .name   = dt_token("img"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rg"),
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[OUTPUT+1].roi,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("grad"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[OUTPUT+1].roi,
                          }},
    };
    int f = dt_module_param_int(module, dt_module_get_param(module->so, dt_token("image")))[0];
    dt_connector_copy(graph, module, 1+3*f+1, id_visn, 0);
    dt_connector_copy(graph, module, OUTPUT+1, id_visn, 1);

    if (1 || module->connector[OUTPUT].roi.scale == 1.0) {
        // no resampling needed

        // connect output
        dt_connector_copy(graph, module, OUTPUT, id_norm, 2);
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
        dt_connector_copy(graph, module, OUTPUT, id_resample, 1);
    }
}
