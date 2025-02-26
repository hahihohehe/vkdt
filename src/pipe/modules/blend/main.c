#include "modules/api.h"
#include <stdio.h>

void commit_params(dt_graph_t *graph, dt_module_t *module)
{
  float *f = (float *)module->committed_param;
  uint32_t *i = (uint32_t *)module->committed_param;
  float *g = (float*)module->param;
  i[0] = module->connector[0].chan == dt_token("rggb") ? module->img_param.filters : (graph->frame_cnt > 1 ? 0u : 1u);
  f[1] = graph->frame == 0 && graph->frame_cnt > 1 ? 0.0f : g[0];
  f[2] = module->img_param.black[0]/(float)0xffff;
  f[3] = module->img_param.white[0]/(float)0xffff;
  f[4] = module->img_param.noise_a;
  f[5] = module->img_param.noise_b;
  f[6] = g[1];
#if 0
  fprintf(stderr, "blend found noise params %g %g\n", f[4], f[5]);
  if(f[4] == 0.0f && f[5] == 0.0f)
  {
    f[4] = 800.0f;
    f[5] = 2.0f;
  }
#endif
}

int init(dt_module_t *mod)
{
  mod->committed_param_size = sizeof(float)*7;
  return 0;
}
