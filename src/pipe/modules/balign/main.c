#include "modules/api.h"


void modify_roi_in(
    dt_graph_t *graph,
    dt_module_t *module)
{
  module->connector[5].roi.wd = module->connector[5].roi.full_wd;
  module->connector[5].roi.ht = module->connector[5].roi.full_ht;
  module->connector[5].roi.scale = 1.0f;
  module->connector[6].roi.wd = module->connector[6].roi.full_wd;
  module->connector[6].roi.ht = module->connector[6].roi.full_ht;
  module->connector[6].roi.scale = 1.0f;
  module->connector[0].roi = module->connector[1].roi;
  module->connector[2].roi = module->connector[1].roi;
  module->connector[3].roi = module->connector[1].roi;
  module->connector[4].roi = module->connector[1].roi;
}

void modify_roi_out(
    dt_graph_t *graph,
    dt_module_t *module)
{
  module->connector[1].roi = module->connector[0].roi;
  module->connector[3].roi = module->connector[0].roi;
}


void
create_nodes(
    dt_graph_t  *graph,
    dt_module_t *module)
{

  // connect each mosaic input to half, generate grey lum map for both input images
  // by a sequence of half, down4, down4, down4 kernels.
  // then compute distance (dist kernel) coarse to fine, merge best offsets (merge kernel),
  // input best coarse offsets to next finer level, and finally output offsets on finest scale.
  dt_roi_t roi[5] = {module->connector[0].roi};
  int block = module->img_param.filters == 9u ? 3 : (module->img_param.filters == 0 ? 1 : 2);


  for(int i=1;i<5;i++)
  {
    int scale = i == 1 ? block : i == 2 ? 2 : 4;
    roi[i] = roi[i-1];
    roi[i].full_wd += scale-1;
    roi[i].full_wd /= scale;
    roi[i].full_ht += scale-1;
    roi[i].full_ht /= scale;
    roi[i].wd += scale-1;
    roi[i].wd /= scale;
    roi[i].ht += scale-1;
    roi[i].ht /= scale;
  }

  dt_roi_t roi_off[5] = {0};
  memcpy(roi_off, roi, 5 * sizeof(dt_roi_t));

  for(int i=0;i<=5;i++)
  {
    int tile_size = i == 4 ? 8 : i == 0 ? 32 : 16;
    roi_off[i].full_wd += tile_size-1;
    roi_off[i].full_wd /= tile_size;
    roi_off[i].full_ht += tile_size-1;
    roi_off[i].full_ht /= tile_size;
    roi_off[i].wd += tile_size-1;
    roi_off[i].wd /= tile_size;
    roi_off[i].ht += tile_size-1;
    roi_off[i].ht /= tile_size;
  }


  int id_down[2][4] = {0};
  const dt_image_params_t *img_param = dt_module_get_input_img_param(graph, module, dt_token("input"));
  if(!img_param) return;
  uint32_t *blacki = (uint32_t *)img_param->black;
  uint32_t *whitei = (uint32_t *)img_param->white;
  for(int k=0;k<2;k++)
  {
    assert(graph->num_nodes < graph->max_nodes);
    id_down[k][0] = graph->num_nodes++;
    graph->node[id_down[k][0]] = (dt_node_t) {
      .name   = dt_token("balign"),
      .kernel = dt_token("half"),
      .module = module,
      .wd     = roi[1].wd,
      .ht     = roi[1].ht,
      .dp     = 1,
      .num_connectors = 2,
      .connector = {{
        .name   = dt_token("input"),
        .type   = dt_token("read"),
        .chan   = module->img_param.filters == 0 ? dt_token("rgba") : dt_token("rggb"),
        .format = module->connector[k].format,
        .roi    = roi[0],
        .connected_mi = -1,
      },{
        .name   = dt_token("output"),
        .type   = dt_token("write"),
        .chan   = dt_token("y"),
        .format = dt_token("f16"),
        .roi    = roi[1],
      }},
      .push_constant_size = 9*sizeof(uint32_t),
      .push_constant = { blacki[0], blacki[1], blacki[2], blacki[3],
        whitei[0], whitei[1], whitei[2], whitei[3],
        img_param->filters },
    };
    dt_connector_copy(graph, module, k == 0 ? 2 : 0, id_down[k][0], 0);

    for(int i=1;i<4;i++)
    {
      assert(graph->num_nodes < graph->max_nodes);
      id_down[k][i] = graph->num_nodes++;
      graph->node[id_down[k][i]] = (dt_node_t) {
        .name   = dt_token("balign"),
        .kernel = i == 1 ? dt_token("down2") : dt_token("down4"),
        .module = module,
        .wd     = roi[i+1].wd,
        .ht     = roi[i+1].ht,
        .dp     = 1,
        .num_connectors = 2,
        .connector = {{
          .name   = dt_token("input"),
          .type   = dt_token("read"),
          .chan   = dt_token("y"),
          .format = dt_token("f16"),
          .roi    = roi[i],
          .connected_mi = -1,
        },{
          .name   = dt_token("output"),
          .type   = dt_token("write"),
          .chan   = dt_token("y"),
          .format = dt_token("f16"),
          .roi    = roi[i+1],
        }},
      };
      CONN(dt_node_connect(graph, id_down[k][i-1], 1, id_down[k][i], 0));
    }
  }

  int id_off[4] = {0};

  for (int k = 3; k >= 0; k--)
  {
    assert(graph->num_nodes < graph->max_nodes);
    id_off[k] = graph->num_nodes++;
    graph->node[id_off[k]] = (dt_node_t) {
      .name   = dt_token("balign"),
      .kernel = dt_token("dist"),
      .module = module,
      .wd     = roi_off[k+1].wd,
      .ht     = roi_off[k+1].ht,
      .dp     = 1,
      .num_connectors = 4,
      .connector = {{
            .name   = dt_token("input0"),
            .type   = dt_token("read"),
            .chan   = dt_token("y"),
            .format = dt_token("f16"),
            .roi    = roi[k+1],
            .connected_mi = -1,
        },{
            .name   = dt_token("input1"),
            .type   = dt_token("read"),
            .chan   = dt_token("y"),
            .format = dt_token("f16"),
            .roi    = roi[k+1],
            .connected_mi = -1,
        },{
            .name   = dt_token("off_in"),
            .type   = dt_token("read"),
            .chan   = k == 3 ? dt_token("y") : dt_token("rg"),  // dummy for k=3
            .format = dt_token("f16"),
            .roi    = k == 3 ? roi[k+1] : roi_off[k+2], // dummy for k=3
            .connected_mi = -1,
        },{
            .name   = dt_token("off_out"),
            .type   = dt_token("write"),
            .chan   = dt_token("rg"),
            .format = dt_token("f16"),
            .roi    = roi_off[k+1],
        }},
        .push_constant_size = 1*sizeof(uint32_t),
        .push_constant = { k },
    };

    CONN(dt_node_connect(graph, id_down[0][k], 1, id_off[k], 0));
    CONN(dt_node_connect(graph, id_down[1][k], 1, id_off[k], 1));

    if (k == 3)
    {
      CONN(dt_node_connect(graph, id_down[0][k], 1, id_off[k], 2)); // dummy input
    }
    else
    {
      CONN(dt_node_connect(graph, id_off[k+1], 3, id_off[k], 2)); // pass on mv
    }
  }

  assert(graph->num_nodes < graph->max_nodes);
  int id_up = graph->num_nodes++;
  graph->node[id_up] = (dt_node_t) {
      .name   = dt_token("balign"),
      .kernel = dt_token("upoff"),
      .module = module,
      .wd     = roi[0].wd,
      .ht     = roi[0].ht,
      .dp     = 1,
      .num_connectors = 3,
      .connector = {{
                        .name   = dt_token("input"),
                        .type   = dt_token("read"),
                        .chan   = dt_token("rg"),
                        .format = dt_token("f16"),
                        .roi    = roi_off[1],
                        .connected_mi = -1,
                    },
                    {
                        .name   = dt_token("output"),
                        .type   = dt_token("write"),
                        .chan   = dt_token("rg"),
                        .format = dt_token("f16"),
                        .roi    = roi[0],
                    },
                    {
                        .name   = dt_token("mask"),
                        .type   = dt_token("write"),
                        .chan   = dt_token("y"),
                        .format = dt_token("f16"),
                        .roi    = roi[0],
                    }},
  };
  CONN(dt_node_connect(graph, id_off[0], 3, id_up, 0));
  dt_connector_copy(graph, module, 1, id_up, 1);
  dt_connector_copy(graph, module, 3, id_up, 2);  // connect dummy mask for now

}
