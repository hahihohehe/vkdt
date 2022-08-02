#include "modules/api.h"

void modify_roi_in(
    dt_graph_t *graph,
    dt_module_t *module)
{
  module->connector[0].roi.wd = module->connector[0].roi.full_wd;
  module->connector[0].roi.ht = module->connector[0].roi.full_ht;
  module->connector[0].roi.scale = 1;
}

void modify_roi_out(
    dt_graph_t *graph,
    dt_module_t *module)
{
  const int block = module->img_param.filters == 9u ? 3 : (module->img_param.filters == 0 ? 1 : 2);
  int p_down = *dt_module_param_int(module, 0);

  module->connector[1].roi.full_wd = module->connector[0].roi.full_wd / (p_down);
  module->connector[1].roi.full_ht = module->connector[0].roi.full_ht / (p_down);
  //module->connector[1].roi.full_wd = module->connector[0].roi.full_wd / (4);
  // module->connector[1].roi.full_ht = module->connector[0].roi.full_ht / (4);

  module->connector[1].roi.wd = module->connector[1].roi.full_wd;
  module->connector[1].roi.ht = module->connector[1].roi.full_ht;
  module->connector[1].roi.scale = 1;
}

dt_graph_run_t check_params(
    dt_module_t *module,
    uint32_t     parid,
    void        *oldval)
{
  return s_graph_run_all;
}

void
create_nodes(
    dt_graph_t  *graph,
    dt_module_t *module)
{
  const int block = module->img_param.filters == 9u ? 3 : (module->img_param.filters == 0 ? 1 : 2);
  const dt_image_params_t *img_param = dt_module_get_input_img_param(graph, module, dt_token("input"));
  if(!img_param) return;

  assert(graph->num_nodes < graph->max_nodes);
  const int id_main = graph->num_nodes++;
  graph->node[id_main] = (dt_node_t) {
      .name   = dt_token("down"),
      .kernel = dt_token("main"),
      .module = module,
      .wd     = module->connector[1].roi.wd,
      .ht     = module->connector[1].roi.ht,
      .dp     = 1,
      .num_connectors = 2,
      .connector = {{
                        .name   = dt_token("input"),
                        .type   = dt_token("read"),
                        .chan   = module->img_param.filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                        .format = module->connector[0].format,
                        .roi    = module->connector[0].roi,
                        .connected_mi = -1,
                    },{
                        .name   = dt_token("output"),
                        .type   = dt_token("write"),
                        .chan   = module->img_param.filters == 0 ? dt_token("rgba") : dt_token("rggb"),
                        .format = dt_token("f16"),
                        .roi    = module->connector[1].roi,
                    }},
      .push_constant_size = 1*sizeof(int),
      .push_constant = { block },
  };
  dt_connector_copy(graph, module, 0, id_main, 0);
  dt_connector_copy(graph, module, 1, id_main, 1);
}
