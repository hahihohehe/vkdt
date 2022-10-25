#include "modules/api.h"
#include "config.h"

void modify_roi_out(
        dt_graph_t *graph,
        dt_module_t *module) {
    dt_roi_t roi_out = module->connector[0].roi;
    roi_out.full_wd = 1000;
    roi_out.full_ht = 1000;
    roi_out.wd = 1000;
    roi_out.ht = 1000;
    roi_out.scale = 1.0f;

    module->connector[0].roi = roi_out;
    module->img_param.filters = 0;
}

void
create_nodes(
        dt_graph_t *graph,
        dt_module_t *module) {
    assert(graph->num_nodes < graph->max_nodes);
    int id_create = graph->num_nodes++;
    graph->node[id_create] = (dt_node_t) {
            .name   = dt_token("vgauss"),
            .kernel = dt_token("create"),
            .module = module,
            .wd     = module->connector[0].roi.wd,
            .ht     = module->connector[0].roi.ht,
            .dp     = 1,
            .num_connectors = 1,
            .connector = {
                          {
                                  .name   = dt_token("output"),
                                  .type   = dt_token("write"),
#ifdef DT_USMPL_RGGB
                                  .chan   = dt_token("rggb"),
#else
                                  .chan   = dt_token("rgba"),
#endif
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[0].roi,
                    }},
    };
    dt_connector_copy(graph, module, 0, id_create, 0);
}
