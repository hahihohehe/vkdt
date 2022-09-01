#include "modules/api.h"
#include "config.h"

dt_graph_run_t
check_params(
    dt_module_t *module,
    uint32_t     parid,
    void        *oldval)
{
    return s_graph_run_all;
}

void modify_roi_in(
        dt_graph_t *graph,
        dt_module_t *module)
{
    module->connector[2].roi.wd = module->connector[2].roi.full_wd;
    module->connector[2].roi.ht = module->connector[2].roi.full_ht;
    module->connector[2].roi.scale = 1.0f;

    module->connector[0].roi.wd = module->connector[0].roi.full_wd;
    module->connector[0].roi.ht = module->connector[0].roi.full_ht;
    module->connector[0].roi.scale = 1.0f;
}

void modify_roi_out(
        dt_graph_t *graph,
        dt_module_t *module)
{
    dt_roi_t roi_out = module->connector[0].roi;
    roi_out.full_wd += 1;
    roi_out.full_wd /= 2;
    roi_out.full_ht += 1;
    roi_out.full_ht /= 2;
    roi_out.wd += 1;
    roi_out.wd /= 2;
    roi_out.ht += 1;
    roi_out.ht /= 2;

    module->connector[1].roi = roi_out;
    module->connector[2].roi = module->connector[0].roi;
}

void
create_nodes(
    dt_graph_t  *graph,
    dt_module_t *module)
{
    const int block = module->img_param.filters == 9u ? 3 : (module->img_param.filters == 0 ? 1 : 2);

    assert(graph->num_nodes < graph->max_nodes);
    int id_guide = graph->num_nodes++;
    graph->node[id_guide] = (dt_node_t) {
            .name   = dt_token("guide"),
            .kernel = dt_token("guide"),
            .module = module,
            .wd     = module->connector[1].roi.wd,
            .ht     = module->connector[1].roi.ht,
            .dp     = 1,
            .num_connectors = 3,
            .connector = {{
                                  .name   = dt_token("input"),
                                  .type   = dt_token("read"),
                                  .chan   = module->img_param.filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                                  .format = module->connector[0].format,
                                  .roi    = module->connector[0].roi,
                                  .connected_mi = -1,
                          },{
                                  .name   = dt_token("mv"),
                                  .type   = dt_token("read"),
                                  .chan   = dt_token("rg"),
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[0].roi,
                          },{
                                  .name   = dt_token("output"),
                                  .type   = dt_token("write"),
                                  .chan   = dt_token("rgba"),
                                  .format = dt_token("f16"),
                                  .roi    = module->connector[1].roi,
                          }},
            .push_constant_size = 2*sizeof(uint32_t),
            .push_constant = { block, 1 },
    };
    dt_connector_copy(graph, module, 0, id_guide, 0);
    dt_connector_copy(graph, module, 1, id_guide, 2);
    dt_connector_copy(graph, module, 2, id_guide, 1);
}
