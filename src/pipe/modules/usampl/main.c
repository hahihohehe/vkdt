#include "modules/api.h"
#include "config.h"

dt_graph_run_t
check_params(
        dt_module_t *module,
        uint32_t parid,
        void *oldval) {
    return s_graph_run_all;
}

void modify_roi_in(
        dt_graph_t *graph,
        dt_module_t *module) {
    module->connector[0].roi.wd = module->connector[0].roi.full_wd;
    module->connector[0].roi.ht = module->connector[0].roi.full_ht;
    module->connector[0].roi.scale = 1.0f;
}

void modify_roi_out(
        dt_graph_t *graph,
        dt_module_t *module) {
    int down = dt_module_param_int(module, dt_module_get_param(module->so, dt_token("down")))[0];
    dt_roi_t roi_out = module->connector[0].roi;
    roi_out.full_wd /= down;
    roi_out.full_ht /= down;
    roi_out.wd /= down;
    roi_out.ht /= down;
    roi_out.scale = 1.0f;

    module->connector[1].roi = roi_out;
    module->connector[2].roi = roi_out;

    module->img_param.filters = 2;
}

void
create_nodes(
        dt_graph_t *graph,
        dt_module_t *module) {
    assert(graph->num_nodes < graph->max_nodes);
    int id_down = graph->num_nodes++;
    graph->node[id_down] = (dt_node_t) {
            .name   = dt_token("usampl"),
            .kernel = dt_token("down"),
            .module = module,
            .wd     = module->connector[1].roi.wd,
            .ht     = module->connector[1].roi.ht,
            .dp     = 1,
            .num_connectors = 3,
            .connector = {{
                                  .name   = dt_token("input"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rgba"),
                                  .format = module->connector[0].format,
                                  .roi    = module->connector[0].roi,
                                  .connected_mi = -1,
                          },
                          {
                                  .name   = dt_token("output"),
                                  .type   = dt_token("write"),
#ifdef DT_USMPL_RGGB
                                  .chan   = dt_token("rggb"),
#else
                                  .chan   = dt_token("rgba"),
#endif
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[1].roi,
                    },
                          {
                                  .name   = dt_token("mv"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rg"),
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[1].roi,
                          }},
    };
    dt_connector_copy(graph, module, 0, id_down, 0);
    dt_connector_copy(graph, module, 1, id_down, 1);
    dt_connector_copy(graph, module, 2, id_down, 2);
}
