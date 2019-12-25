extern "C" {
#include "gui.h"
#include "view.h"
#include "qvk/qvk.h"
#include "pipe/modules/api.h"
}
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_sdl.h"
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>

// XXX argh, what a terrible hack
#define dt_log fprintf
#define s_log_qvk stderr

// some ui state (probably clean up and put in a struct or so
namespace { // anonymous gui state namespace
static int g_active_widget = -1;
static float g_state[2100] = {0.0f};
static float *g_mapped = 0;
static int g_lod = 0;
static float g_connector[100][30][2];

void widget_end()
{
  if(g_active_widget < 0) return; // all good already
  int i = g_active_widget;
  if(g_mapped)
  {
    g_mapped = 0;
  }
  else
  {
    int modid = vkdt.widget[i].modid;
    int parid = vkdt.widget[i].parid;
    const dt_ui_param_t *p = vkdt.graph_dev.module[modid].so->param[parid];
    float *v = (float*)(vkdt.graph_dev.module[modid].param + p->offset);
    size_t size = dt_ui_param_size(p->type, p->cnt);
    memcpy(v, g_state, size);
  }
  g_active_widget = -1;
  vkdt.graph_dev.runflags = s_graph_run_all;
}
} // end anonymous gui state space

// remove this once we have a gui struct!
extern "C" void dt_gui_set_lod(int lod)
{
  // set graph output scale factor and
  // trigger complete pipeline rebuild
  if(g_lod > 1)
  {
    vkdt.graph_dev.output_wd = vkdt.state.center_wd / (g_lod-1);
    vkdt.graph_dev.output_ht = vkdt.state.center_ht / (g_lod-1);
  }
  else
  {
    vkdt.graph_dev.output_wd = 0;
    vkdt.graph_dev.output_ht = 0;
  }
  vkdt.graph_dev.runflags = static_cast<dt_graph_run_t>(-1u);
  // reset view? would need to set zoom, too
  vkdt.state.look_at_x = FLT_MAX;
  vkdt.state.look_at_y = FLT_MAX;
  vkdt.state.scale = -1;
}

namespace {
void view_to_image(
    const float v[2],
    float       img[2])
{
  dt_node_t *out = dt_graph_get_display(&vkdt.graph_dev, dt_token("main"));
  assert(out);
  float wd  = (float)out->connector[0].roi.wd;
  float ht  = (float)out->connector[0].roi.ht;
  float fwd = (float)out->connector[0].roi.full_wd/out->connector[0].roi.scale;
  float fht = (float)out->connector[0].roi.full_ht/out->connector[0].roi.scale;
  float imwd = vkdt.state.center_wd, imht = vkdt.state.center_ht;
  float scale = MIN(imwd/wd, imht/ht);
  if(vkdt.state.scale > 0.0f) scale = vkdt.state.scale;
  float cvx = vkdt.state.center_wd *.5f;
  float cvy = vkdt.state.center_ht *.5f;
  if(vkdt.state.look_at_x == FLT_MAX) vkdt.state.look_at_x = wd/2.0f;
  if(vkdt.state.look_at_y == FLT_MAX) vkdt.state.look_at_y = ht/2.0f;
  float ox = cvx - scale * vkdt.state.look_at_x;
  float oy = cvy - scale * vkdt.state.look_at_y;
  float x = ox + vkdt.state.center_x, y = oy + vkdt.state.center_y;
  img[0] = (v[0] - x) / (scale * fwd);
  img[1] = (v[1] - y) / (scale * fht);
}

// convert normalised image coordinates to pixel coord on screen
void image_to_view(
    const float img[2], // image pixel coordinate in [0,1]^2
    float       v[2])   // window pixel coordinate
{
  dt_node_t *out = dt_graph_get_display(&vkdt.graph_dev, dt_token("main"));
  assert(out);
  float wd  = (float)out->connector[0].roi.wd;
  float ht  = (float)out->connector[0].roi.ht;
  float fwd = (float)out->connector[0].roi.full_wd/out->connector[0].roi.scale;
  float fht = (float)out->connector[0].roi.full_ht/out->connector[0].roi.scale;
  float imwd = vkdt.state.center_wd, imht = vkdt.state.center_ht;
  float scale = MIN(imwd/wd, imht/ht);
  if(vkdt.state.scale > 0.0f) scale = vkdt.state.scale;
  float cvx = vkdt.state.center_wd *.5f;
  float cvy = vkdt.state.center_ht *.5f;
  if(vkdt.state.look_at_x == FLT_MAX) vkdt.state.look_at_x = wd/2.0f;
  if(vkdt.state.look_at_y == FLT_MAX) vkdt.state.look_at_y = ht/2.0f;
  float ox = cvx - scale * vkdt.state.look_at_x;
  float oy = cvy - scale * vkdt.state.look_at_y;
  float x = ox + vkdt.state.center_x, y = oy + vkdt.state.center_y;
  v[0] = x + scale * img[0] * fwd;
  v[1] = y + scale * img[1] * fht;
}

inline ImVec4 gamma(ImVec4 in)
{
  // theme colours are given as float sRGB values in imgui, while we will
  // draw them in linear (depending on qvk config)
  // return ImVec4(pow(in.x, 2.2), pow(in.y, 2.2), pow(in.z, 2.2), in.w);
  // in the current configuration, we need no correction:
  return in;
}

inline void dark_corporate_style()
{
  ImGuiStyle & style = ImGui::GetStyle();
  ImVec4 * colors = style.Colors;

	/// 0 = FLAT APPEARENCE
	/// 1 = MORE "3D" LOOK
	int is3D = 0;

	colors[ImGuiCol_Text]                   = gamma(ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
	colors[ImGuiCol_TextDisabled]           = gamma(ImVec4(0.40f, 0.40f, 0.40f, 1.00f));
	colors[ImGuiCol_ChildBg]                = gamma(ImVec4(0.25f, 0.25f, 0.25f, 1.00f));
	colors[ImGuiCol_WindowBg]               = gamma(ImVec4(0.25f, 0.25f, 0.25f, 1.00f));
	colors[ImGuiCol_PopupBg]                = gamma(ImVec4(0.25f, 0.25f, 0.25f, 1.00f));
	colors[ImGuiCol_Border]                 = gamma(ImVec4(0.12f, 0.12f, 0.12f, 0.71f));
	colors[ImGuiCol_BorderShadow]           = gamma(ImVec4(1.00f, 1.00f, 1.00f, 0.06f));
	colors[ImGuiCol_FrameBg]                = gamma(ImVec4(0.42f, 0.42f, 0.42f, 0.54f));
	colors[ImGuiCol_FrameBgHovered]         = gamma(ImVec4(0.42f, 0.42f, 0.42f, 0.40f));
	colors[ImGuiCol_FrameBgActive]          = gamma(ImVec4(0.56f, 0.56f, 0.56f, 0.67f));
	colors[ImGuiCol_TitleBg]                = gamma(ImVec4(0.19f, 0.19f, 0.19f, 1.00f));
	colors[ImGuiCol_TitleBgActive]          = gamma(ImVec4(0.22f, 0.22f, 0.22f, 1.00f));
	colors[ImGuiCol_TitleBgCollapsed]       = gamma(ImVec4(0.17f, 0.17f, 0.17f, 0.90f));
	colors[ImGuiCol_MenuBarBg]              = gamma(ImVec4(0.335f, 0.335f, 0.335f, 1.000f));
	colors[ImGuiCol_ScrollbarBg]            = gamma(ImVec4(0.24f, 0.24f, 0.24f, 0.53f));
	colors[ImGuiCol_ScrollbarGrab]          = gamma(ImVec4(0.41f, 0.41f, 0.41f, 1.00f));
	colors[ImGuiCol_ScrollbarGrabHovered]   = gamma(ImVec4(0.52f, 0.52f, 0.52f, 1.00f));
	colors[ImGuiCol_ScrollbarGrabActive]    = gamma(ImVec4(0.76f, 0.76f, 0.76f, 1.00f));
	colors[ImGuiCol_CheckMark]              = gamma(ImVec4(0.65f, 0.65f, 0.65f, 1.00f));
	colors[ImGuiCol_SliderGrab]             = gamma(ImVec4(0.52f, 0.52f, 0.52f, 1.00f));
	colors[ImGuiCol_SliderGrabActive]       = gamma(ImVec4(0.64f, 0.64f, 0.64f, 1.00f));
	colors[ImGuiCol_Button]                 = gamma(ImVec4(0.54f, 0.54f, 0.54f, 0.35f));
	colors[ImGuiCol_ButtonHovered]          = gamma(ImVec4(0.52f, 0.52f, 0.52f, 0.59f));
	colors[ImGuiCol_ButtonActive]           = gamma(ImVec4(0.76f, 0.76f, 0.76f, 1.00f));
	colors[ImGuiCol_Header]                 = gamma(ImVec4(0.38f, 0.38f, 0.38f, 1.00f));
	colors[ImGuiCol_HeaderHovered]          = gamma(ImVec4(0.47f, 0.47f, 0.47f, 1.00f));
	colors[ImGuiCol_HeaderActive]           = gamma(ImVec4(0.76f, 0.76f, 0.76f, 0.77f));
	colors[ImGuiCol_Separator]              = gamma(ImVec4(0.000f, 0.000f, 0.000f, 0.137f));
	colors[ImGuiCol_SeparatorHovered]       = gamma(ImVec4(0.700f, 0.671f, 0.600f, 0.290f));
	colors[ImGuiCol_SeparatorActive]        = gamma(ImVec4(0.702f, 0.671f, 0.600f, 0.674f));
	colors[ImGuiCol_ResizeGrip]             = gamma(ImVec4(0.26f, 0.59f, 0.98f, 0.25f));
	colors[ImGuiCol_ResizeGripHovered]      = gamma(ImVec4(0.26f, 0.59f, 0.98f, 0.67f));
	colors[ImGuiCol_ResizeGripActive]       = gamma(ImVec4(0.26f, 0.59f, 0.98f, 0.95f));
	colors[ImGuiCol_PlotLines]              = gamma(ImVec4(0.61f, 0.61f, 0.61f, 1.00f));
	colors[ImGuiCol_PlotLinesHovered]       = gamma(ImVec4(1.00f, 0.43f, 0.35f, 1.00f));
	colors[ImGuiCol_PlotHistogram]          = gamma(ImVec4(0.90f, 0.70f, 0.00f, 1.00f));
	colors[ImGuiCol_PlotHistogramHovered]   = gamma(ImVec4(1.00f, 0.60f, 0.00f, 1.00f));
	colors[ImGuiCol_TextSelectedBg]         = gamma(ImVec4(0.73f, 0.73f, 0.73f, 0.35f));
	colors[ImGuiCol_ModalWindowDimBg]       = gamma(ImVec4(0.80f, 0.80f, 0.80f, 0.35f));
	colors[ImGuiCol_DragDropTarget]         = gamma(ImVec4(1.00f, 1.00f, 0.00f, 0.90f));
	colors[ImGuiCol_NavHighlight]           = gamma(ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
	colors[ImGuiCol_NavWindowingHighlight]  = gamma(ImVec4(1.00f, 1.00f, 1.00f, 0.70f));
	colors[ImGuiCol_NavWindowingDimBg]      = gamma(ImVec4(0.80f, 0.80f, 0.80f, 0.20f));

	style.PopupRounding = 3;

	style.WindowPadding = ImVec2(4, 4);
	style.FramePadding  = ImVec2(6, 4);
	style.ItemSpacing   = ImVec2(6, 2);

	style.ScrollbarSize = 18;

	style.WindowBorderSize = 1;
	style.ChildBorderSize  = 1;
	style.PopupBorderSize  = 1;
	style.FrameBorderSize  = is3D;

	style.WindowRounding    = 0;
	style.ChildRounding     = 0;
	style.FrameRounding     = 0;
	style.ScrollbarRounding = 2;
	style.GrabRounding      = 3;

	#ifdef IMGUI_HAS_DOCK
		style.TabBorderSize = is3D;
		style.TabRounding   = 3;

		colors[ImGuiCol_DockingEmptyBg]     = gamma(ImVec4(0.38f, 0.38f, 0.38f, 1.00f));
		colors[ImGuiCol_Tab]                = gamma(ImVec4(0.25f, 0.25f, 0.25f, 1.00f));
		colors[ImGuiCol_TabHovered]         = gamma(ImVec4(0.40f, 0.40f, 0.40f, 1.00f));
		colors[ImGuiCol_TabActive]          = gamma(ImVec4(0.33f, 0.33f, 0.33f, 1.00f));
		colors[ImGuiCol_TabUnfocused]       = gamma(ImVec4(0.25f, 0.25f, 0.25f, 1.00f));
		colors[ImGuiCol_TabUnfocusedActive] = gamma(ImVec4(0.33f, 0.33f, 0.33f, 1.00f));
		colors[ImGuiCol_DockingPreview]     = gamma(ImVec4(0.85f, 0.85f, 0.85f, 0.28f));

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
	#endif
}
} // anonymous namespace

extern "C" int dt_gui_init_imgui()
{
  // Setup Dear ImGui context
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

  // Setup Dear ImGui style
  // ImGui::StyleColorsDark();
  // ImGui::StyleColorsClassic();
  dark_corporate_style();

  // Setup Platform/Renderer bindings
  ImGui_ImplSDL2_InitForVulkan(qvk.window);
  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = qvk.instance;
  init_info.PhysicalDevice = qvk.physical_device;
  init_info.Device = qvk.device;
  init_info.QueueFamily = qvk.queue_idx_graphics;
  init_info.Queue = qvk.queue_graphics;
  init_info.PipelineCache = vkdt.pipeline_cache;
  init_info.DescriptorPool = vkdt.descriptor_pool;
  init_info.Allocator = 0;
  init_info.MinImageCount = vkdt.min_image_count;
  init_info.ImageCount = vkdt.image_count;
  init_info.CheckVkResultFn = 0;//check_vk_result;
  ImGui_ImplVulkan_Init(&init_info, vkdt.render_pass);

  // Load Fonts
  // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
  // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
  // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
  // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
  // - Read 'misc/fonts/README.txt' for more instructions and details.
  // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
  //io.Fonts->AddFontDefault();
  //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
  //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
  //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
  //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
  //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
  //IM_ASSERT(font != NULL);

  // XXX TODO: move this out to gui.c so we don't need to use dt_log in cpp!
  // XXX maybe just remove the QVK() :/
  // upload Fonts
  {
    // use any command queue
    VkCommandPool command_pool = vkdt.command_pool[0];
    VkCommandBuffer command_buffer = vkdt.command_buffer[0];

    QVK(vkResetCommandPool(qvk.device, command_pool, 0));
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    QVK(vkBeginCommandBuffer(command_buffer, &begin_info));

    ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

    VkSubmitInfo end_info = {};
    end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    end_info.commandBufferCount = 1;
    end_info.pCommandBuffers = &command_buffer;
    QVK(vkEndCommandBuffer(command_buffer));
    QVK(vkQueueSubmit(qvk.queue_graphics, 1, &end_info, VK_NULL_HANDLE));

    QVK(vkDeviceWaitIdle(qvk.device));
    ImGui_ImplVulkan_DestroyFontUploadObjects();
  }
  return 0;
}

extern "C" int dt_gui_poll_event_imgui(SDL_Event *event)
{
  // Poll and handle events (inputs, window resize, etc.)
  // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
  // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
  // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
  // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
  ImGui_ImplSDL2_ProcessEvent(event);

  // TODO: this is darkroom stuff
  // TODO: probably move to darkroom.h and talk to the c++ gui via these g_* buffers
  const float px_dist = 20;

  if(g_active_widget >= 0)
  {
    const int i = g_active_widget;
    switch(vkdt.widget[i].type)
    {
      case dt_token("quad"):
      {
        static int c = -1;
        if(c >= 0 && event->type == SDL_MOUSEMOTION)
        {
          float n[] = {0, 0};
          float v[] = {(float)event->button.x, (float)event->button.y};
          view_to_image(v, n);
          // convert view space mouse coordinate to normalised image
          // copy to quad state at corner c
          g_state[2*c+0] = n[0];
          g_state[2*c+1] = n[1];
          return 1;
        }
        else if(event->type == SDL_MOUSEBUTTONUP)
        {
          c = -1;
        }
        else if(event->type == SDL_MOUSEBUTTONDOWN)
        {
          // find active corner if close enough
          float m[] = {(float)event->button.x, (float)event->button.y};
          float max_dist = FLT_MAX;
          for(int cc=0;cc<4;cc++)
          {
            float n[] = {g_state[2*cc+0], g_state[2*cc+1]}, v[2];
            image_to_view(n, v);
            float dist2 =
              (v[0]-m[0])*(v[0]-m[0])+
              (v[1]-m[1])*(v[1]-m[1]);
            if(dist2 < px_dist*px_dist)
            {
              if(dist2 < max_dist)
              {
                max_dist = dist2;
                c = cc;
              }
            }
          }
          return max_dist < FLT_MAX;
        }
        break;
      }
      case dt_token("axquad"):
      {
        static int e = -1;
        if(e >= 0 && event->type == SDL_MOUSEMOTION)
        {
          float n[] = {0, 0};
          float v[] = {(float)event->button.x, (float)event->button.y};
          view_to_image(v, n);
          float edge = e < 2 ? n[0] : n[1];
          g_state[e] = edge;
          return 1;
        }
        else if(event->type == SDL_MOUSEBUTTONUP)
        {
          e = -1;
        }
        else if(event->type == SDL_MOUSEBUTTONDOWN)
        {
          // find active corner if close enough
          float m[2] = {(float)event->button.x, (float)event->button.y};
          float max_dist = FLT_MAX;
          for(int ee=0;ee<4;ee++)
          {
            float n[] = {ee < 2 ? g_state[ee] : 0, ee >= 2 ? g_state[ee] : 0}, v[2];
            image_to_view(n, v);
            float dist2 =
              ee < 2 ?
              (v[0]-m[0])*(v[0]-m[0]) :
              (v[1]-m[1])*(v[1]-m[1]);
            if(dist2 < px_dist*px_dist)
            {
              if(dist2 < max_dist)
              {
                max_dist = dist2;
                e = ee;
              }
            }
          }
          return max_dist < FLT_MAX;
        }
        break;
      }
      case dt_token("draw"):
      {
        // record mouse position relative to image
        // append to state until 1000 lines
        static int c = -1;
        static int pressed = 0;
        if(c >= 0 && event->type == SDL_MOUSEMOTION &&
            pressed &&
            c < 2004)
        {
          float n[] = {0, 0};
          float v[] = {(float)event->button.x, (float)event->button.y};
          view_to_image(v, n);
          // convert view space mouse coordinate to normalised image
          // copy to quad state at corner c
          g_mapped[1+2*c+0] = n[0];
          g_mapped[1+2*c+1] = n[1];
          if(c == 0 || (c > 0 &&
              fabsf(n[0] - g_mapped[1+2*(c-1)+0]) > 0.004 &&
              fabsf(n[1] - g_mapped[1+2*(c-1)+1]) > 0.004))
            g_mapped[0] = c++;
          return 1;
        }
        else if(event->type == SDL_MOUSEBUTTONUP)
        {
          pressed = 0;
          c = -1;
          return 1;
        }
        else if(event->type == SDL_MOUSEBUTTONDOWN)
        {
          pressed = 1;
          if(c < 0) c = 0;
          // right click: remove stroke
          if(event->button.button == SDL_BUTTON_RIGHT) c = -1;
          return 1;
        }
        break;
      }
      default:;
    }
  }
  return 0;
}

namespace {

void render_lighttable()
{
  ImGuiStyle &style = ImGui::GetStyle();
  // if thumbnails are initialised, draw a couple of them on screen to prove
  // that we've done something:
  { // center image view
    ImGuiWindowFlags window_flags = 0;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    window_flags |= ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoResize;
    window_flags |= ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos (ImVec2(vkdt.state.center_x,  vkdt.state.center_y),  ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vkdt.state.center_wd, vkdt.state.center_ht), ImGuiCond_Always);
    ImGui::Begin("lighttable center", 0, window_flags);

    const int ipl = 6;
    const int border = 0.01 * qvk.win_width;
    const int wd = vkdt.state.center_wd / ipl - border*2 - style.ItemSpacing.x*2;
    const int ht = 0.6 * wd; // XXX probably do square in the future?
    const int cnt = vkdt.db.collection_cnt;
    const int lines = (cnt+ipl-1)/ipl;
    ImGuiListClipper clipper;
    clipper.Begin(lines);
    while(clipper.Step())
    {
      // for whatever reason (gauge sizes?) imgui will always pass [0,1) as a first range.
      // we don't want these to trigger a deferred load.
      // in case [0,1) is within the visible region, however, [1,8) might be the next
      // range, for instance. this means we'll need to do some weird dance to detect it
      // TODO: ^
      // fprintf(stderr, "displaying range %u %u\n", clipper.DisplayStart, clipper.DisplayEnd);
      dt_thumbnails_load_list(
          &vkdt.thumbnails,
          &vkdt.db,
          vkdt.db.collection,
          MIN(clipper.DisplayStart * ipl, vkdt.db.collection_cnt-1),
          MIN(clipper.DisplayEnd   * ipl, vkdt.db.collection_cnt));
      for(int line=clipper.DisplayStart;line<clipper.DisplayEnd;line++)
      {
        int i = line * ipl;
        for(int k=0;k<ipl;k++)
        {
          uint32_t tid = vkdt.db.image[vkdt.db.collection[i]].thumbnail;
          if(tid == -1u) tid = 0;
          bool ret = ImGui::ImageButton(vkdt.thumbnails.thumb[tid].dset,
              ImVec2(wd, ht),
              ImVec2(0,0), ImVec2(1,1),
              border,
              ImVec4(0.5f,0.5f,0.5f,1.0f), ImVec4(1.0f,1.0f,1.0f,1.0f));
          if(ret)
          {
            vkdt.db.current_image = vkdt.db.collection[i];
            dt_view_switch(s_view_darkroom);
          }
          if(k < ipl-1) ImGui::SameLine();
          // else NextColumn()
          if(++i >= cnt) break;
        }
      }
    }
    ImGui::End(); // lt center window
  }
}

void render_module(dt_graph_t *graph, dt_module_t *module)
{
  char name[30];
  snprintf(name, sizeof(name), "%" PRItkn " %" PRItkn,
      dt_token_str(module->name), dt_token_str(module->inst));
  float lineht = ImGui::GetTextLineHeight();
  ImVec2 hp = ImGui::GetCursorScreenPos();
  if(!ImGui::CollapsingHeader(name))
  {
    for(int k=0;k<module->num_connectors;k++)
    {
      g_connector[module - graph->module][k][0] = hp.x + vkdt.state.panel_wd * (0.75f + 0.13f);
      g_connector[module - graph->module][k][1] = hp.y + 0.5f*lineht;
      // this switches off connections of collapsed modules
      // g_connector[module - graph->module][k][0] = -1;
      // g_connector[module - graph->module][k][1] = -1;
    }
    return;
  }
  ImGuiWindowFlags window_flags = 0;
  // window_flags |= ImGuiWindowFlags_NoTitleBar;
  window_flags |= ImGuiWindowFlags_NoMove;
  window_flags |= ImGuiWindowFlags_NoResize;
  window_flags |= ImGuiWindowFlags_NoBackground;
  snprintf(name, sizeof(name), "%" PRItkn "_%" PRItkn "_p", dt_token_str(module->name), dt_token_str(module->inst));
  int ht = lineht * (module->num_connectors + 1);
  ImGui::BeginChild(name, ImVec2(vkdt.state.panel_wd * 0.75, ht), false, window_flags);
  // ImGui::Text("%" PRItkn, dt_token_str(module->name));
  // module menu for pipeline config goes here:
  // TODO
  // ImGui::Button("remove");
  // ImGui::SameLine();
  // ImGui::Button("insert after");
  // ImGui::SameLine();
  // ImGui::Button("blend");

  // ImGui::Button("move up");
  // ImGui::SameLine();
  // ImGui::Button("move down");
  ImGui::EndChild();

  ImGui::SameLine();

  snprintf(name, sizeof(name), "%" PRItkn "_%" PRItkn "_c", dt_token_str(module->name), dt_token_str(module->inst));
  ImGui::BeginChild(name, ImVec2(vkdt.state.panel_wd * 0.25, ht), false, window_flags);

  for(int k=0;k<module->num_connectors;k++)
  {
    if(dt_connector_output(module->connector+k))
    {
      ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::Text("%" PRItkn, dt_token_str(module->connector[k].name));
      g_connector[module - graph->module][k][0] = hp.x + vkdt.state.panel_wd * (0.75f + 0.13f);
      g_connector[module - graph->module][k][1] = p.y + 0.5f*lineht;
    }
  }
  for(int k=0;k<module->num_connectors;k++)
  {
    if(dt_connector_input(module->connector+k))
    {
      ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::Text("%" PRItkn, dt_token_str(module->connector[k].name));
      g_connector[module - graph->module][k][0] = hp.x + vkdt.state.panel_wd * (0.75f + 0.13f);
      g_connector[module - graph->module][k][1] = p.y + 0.5f*lineht;
    }
  }
  ImGui::EndChild();
}

void render_darkroom_favourite()
{
  // streamlined "favourite" ui
  for(int i=0;i<vkdt.num_widgets;i++)
  {
    int modid = vkdt.widget[i].modid;
    int parid = vkdt.widget[i].parid;
    char string[256];
    // distinguish by type:
    switch(vkdt.widget[i].type)
    {
      case dt_token("slider"):
      {
        // TODO: distinguish by count:
        float *val = (float*)(vkdt.graph_dev.module[modid].param + 
          vkdt.graph_dev.module[modid].so->param[parid]->offset);
        char str[10] = {0};
        memcpy(str,
            &vkdt.graph_dev.module[modid].so->param[parid]->name, 8);
        ImGui::SliderFloat(str, val,
            vkdt.widget[i].min,
            vkdt.widget[i].max,
            "%2.5f");
        break;
      }
      case dt_token("quad"):
      {
        float *v = (float*)(vkdt.graph_dev.module[modid].param + 
          vkdt.graph_dev.module[modid].so->param[parid]->offset);
        if(g_active_widget == i)
        {
          snprintf(string, sizeof(string), "%" PRItkn":%" PRItkn" done",
              dt_token_str(vkdt.graph_dev.module[modid].name),
              dt_token_str(vkdt.graph_dev.module[modid].so->param[parid]->name));
          if(ImGui::Button(string)) widget_end();
        }
        else
        {
          snprintf(string, sizeof(string), "%" PRItkn":%" PRItkn" start",
              dt_token_str(vkdt.graph_dev.module[modid].name),
              dt_token_str(vkdt.graph_dev.module[modid].so->param[parid]->name));
          if(ImGui::Button(string))
          {
            widget_end(); // if another one is still in progress, end that now
            g_active_widget = i;
            // copy to quad state
            memcpy(g_state, v, sizeof(float)*8);
            // reset module params so the image will not appear distorted:
            float def[] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
            memcpy(v, def, sizeof(float)*8);
          }
        }
        break;
      }
      case dt_token("axquad"):
      {
        float *v = (float*)(vkdt.graph_dev.module[modid].param + 
          vkdt.graph_dev.module[modid].so->param[parid]->offset);
        if(g_active_widget == i)
        {
          snprintf(string, sizeof(string), "%" PRItkn":%" PRItkn" done",
              dt_token_str(vkdt.graph_dev.module[modid].name),
              dt_token_str(vkdt.graph_dev.module[modid].so->param[parid]->name));
          if(ImGui::Button(string)) widget_end();
        }
        else
        {
          snprintf(string, sizeof(string), "%" PRItkn":%" PRItkn" start",
              dt_token_str(vkdt.graph_dev.module[modid].name),
              dt_token_str(vkdt.graph_dev.module[modid].so->param[parid]->name));
          if(ImGui::Button(string))
          {
            widget_end(); // if another one is still in progress, end that now
            g_active_widget = i;
            // copy to quad state
            memcpy(g_state, v, sizeof(float)*4);
            // reset module params so the image will not appear distorted:
            float def[] = {0.f, 1.f, 0.f, 1.f};
            memcpy(v, def, sizeof(float)*4);
          }
        }
        break;
      }
      case dt_token("draw"):
      {
        float *v = (float*)(vkdt.graph_dev.module[modid].param + 
          vkdt.graph_dev.module[modid].so->param[parid]->offset);
        if(g_active_widget == i)
        {
          snprintf(string, sizeof(string), "%" PRItkn":%" PRItkn" done",
              dt_token_str(vkdt.graph_dev.module[modid].name),
              dt_token_str(vkdt.graph_dev.module[modid].so->param[parid]->name));
          if(ImGui::Button(string)) widget_end();
        }
        else
        {
          snprintf(string, sizeof(string), "%" PRItkn":%" PRItkn" start",
              dt_token_str(vkdt.graph_dev.module[modid].name),
              dt_token_str(vkdt.graph_dev.module[modid].so->param[parid]->name));
          if(ImGui::Button(string))
          {
            widget_end(); // if another one is still in progress, end that now
            g_active_widget = i;
            g_mapped = v; // map state
          }
        }
        break;
      }
      default:;
    }
  }
}

void render_darkroom_full()
{
  ImGui::Text("unimplemented");
}

void render_darkroom_pipeline()
{
  // full featured module + connection ui
  uint32_t mod_id[100];       // module id, including disconnected modules
  uint32_t mod_in[100] = {0}; // module indentation level
  dt_graph_t *graph = &vkdt.graph_dev;
  assert(graph->num_modules < sizeof(mod_id)/sizeof(mod_id[0]));
  for(int k=0;k<graph->num_modules;k++) mod_id[k] = k;
  dt_module_t *const arr = graph->module;
  const int arr_cnt = graph->num_modules;
  int pos = 0, pos2 = 0; // find pos2 as the swapping position, where mod_id[pos2] = curr
#define TRAVERSE_PRE \
  {\
    pos2 = curr;\
    while(mod_id[pos2] != curr) pos2 = mod_id[pos2];\
    int tmp = mod_id[pos];\
    mod_id[pos++] = mod_id[pos2];\
    mod_id[pos2] = tmp;\
    render_module(graph, arr+curr);\
  }
#include "pipe/graph-traverse.inc"

  // now draw the disconnected modules
  for(int m=pos;m<graph->num_modules;m++)
    render_module(graph, arr+mod_id[m]);

  // draw connectors outside of clipping region of individual widgets, on top.
  // also go through list in reverse order such that the first connector will
  // pick up the largest indentation to avoid most crossovers
  for(int mi=graph->num_modules-1;mi>=0;mi--)
  {
    int m = mod_id[mi];
    for(int k=graph->module[m].num_connectors-1;k>=0;k--)
    {
      if(dt_connector_input(graph->module[m].connector+k))
      {
        const float *p = g_connector[m][k];
        int nid = graph->module[m].connector[k].connected_mi;
        int cid = graph->module[m].connector[k].connected_mc;
        const float *q = g_connector[nid][cid];
        float b = vkdt.state.panel_wd * 0.02;
        int rev = nid; // TODO: store reverse list?
        while(mod_id[rev] != nid) rev = mod_id[rev];
        // traverse mod_id list between mi and rev nid and get indentation level
        int ident = 0;
        if(mi < rev) for(int i=mi+1;i<rev;i++)
        {
          mod_in[i] ++;
          ident = MAX(mod_in[i], ident);
        }
        else for(int i=rev+1;i<mi;i++)
        {
          mod_in[i] ++;
          ident = MAX(mod_in[i], ident);
        }
        b *= ident + 1;
        if(p[0] == -1 || q[0] == -1) continue;
        float x[8] = {
          p[0], p[1], p[0]+b, p[1],
          q[0]+b, q[1], q[0], q[1],
        };
        ImGui::GetWindowDrawList()->AddPolyline(
            (ImVec2 *)x, 4, IM_COL32_WHITE, false, 1.0);
      }
    }
  }
}

void render_darkroom()
{
  { // center image view
    ImGuiWindowFlags window_flags = 0;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    window_flags |= ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoResize;
    window_flags |= ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos (ImVec2(vkdt.state.center_x,  vkdt.state.center_y),  ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vkdt.state.center_wd, vkdt.state.center_ht), ImGuiCond_Always);
    ImGui::Begin("darkroom center", 0, window_flags);

    // draw center view image:
    dt_node_t *out_main = dt_graph_get_display(&vkdt.graph_dev, dt_token("main"));
    if(out_main)
    {
      ImTextureID imgid = out_main->dset;
      float im0[2], im1[2];
      float v0[2] = {(float)vkdt.state.center_x, (float)vkdt.state.center_y};
      float v1[2] = {(float)vkdt.state.center_x+vkdt.state.center_wd,
        (float)vkdt.state.center_y+vkdt.state.center_ht};
      view_to_image(v0, im0);
      view_to_image(v1, im1);
      im0[0] = CLAMP(im0[0], 0.0f, 1.0f);
      im0[1] = CLAMP(im0[1], 0.0f, 1.0f);
      im1[0] = CLAMP(im1[0], 0.0f, 1.0f);
      im1[1] = CLAMP(im1[1], 0.0f, 1.0f);
      image_to_view(im0, v0);
      image_to_view(im1, v1);
      ImGui::GetWindowDrawList()->AddImage(
          imgid, ImVec2(v0[0], v0[1]), ImVec2(v1[0], v1[1]),
          ImVec2(im0[0], im0[1]), ImVec2(im1[0], im1[1]), IM_COL32_WHITE);
    }
    // center view has on-canvas widgets:
    if(g_active_widget >= 0)
    {
      const int i = g_active_widget;
      // distinguish by type:
      switch(vkdt.widget[i].type)
      {
        case dt_token("quad"):
        {
          float *v = g_state;
          float p[8];
          for(int k=0;k<4;k++)
            image_to_view(v+2*k, p+2*k);
          ImGui::GetWindowDrawList()->AddPolyline(
              (ImVec2 *)p, 4, IM_COL32_WHITE, true, 1.0);
          break;
        }
        case dt_token("axquad"):
        {
          float v[8] = {
            g_state[0], g_state[2], g_state[1], g_state[2], 
            g_state[1], g_state[3], g_state[0], g_state[3]
          };
          float p[8];
          for(int k=0;k<4;k++)
            image_to_view(v+2*k, p+2*k);
          ImGui::GetWindowDrawList()->AddPolyline(
              (ImVec2 *)p, 4, IM_COL32_WHITE, true, 1.0);
          break;
        }
        case dt_token("draw"):
        { // this is not really needed. draw line on top of stroke.
          // we map the buffer and get instant feedback on the image.
          float p[2004];
          int cnt = g_mapped[0];
          for(int k=0;k<cnt;k++)
          {
            p[2*k+0] = g_mapped[1+2*k+0];
            p[2*k+1] = g_mapped[1+2*k+1];
            image_to_view(p+2*k, p+2*k);
          }
          ImGui::GetWindowDrawList()->AddPolyline(
              (ImVec2 *)p, cnt, IM_COL32_WHITE, false, 1.0);
          break;
        }
        default:;
      }
    }
    ImGui::End();
  }

  { // right panel
    ImGuiWindowFlags window_flags = 0;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    // if (no_scrollbar)       window_flags |= ImGuiWindowFlags_NoScrollbar;
    // window_flags |= ImGuiWindowFlags_MenuBar;
    window_flags |= ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoResize;
    // if (no_collapse)        window_flags |= ImGuiWindowFlags_NoCollapse;
    // if (no_nav)             window_flags |= ImGuiWindowFlags_NoNav;
    // if (no_background)      window_flags |= ImGuiWindowFlags_NoBackground;
    // if (no_bring_to_front)  window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowPos (ImVec2(qvk.win_width - vkdt.state.panel_wd, 0),    ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vkdt.state.panel_wd, vkdt.state.panel_ht), ImGuiCond_Always);
    ImGui::Begin("panel-right", 0, window_flags);

    // draw histogram image:
    dt_node_t *out_hist = dt_graph_get_display(&vkdt.graph_dev, dt_token("hist"));
    if(out_hist)
    {
      int border = 0.01 * qvk.win_width;
      int wd = vkdt.state.panel_wd - border;
      int ht = wd * 2.0f/3.0f;
      ImGui::Image(out_hist->dset,
          ImVec2(wd, ht),
          ImVec2(0,0), ImVec2(1,1),
          ImVec4(1.0f,1.0f,1.0f,1.0f), ImVec4(1.0f,1.0f,1.0f,0.5f));
    }

    if(ImGui::SliderInt("LOD", &g_lod, 1, 16, "%d"))
    { // LOD switcher
      dt_gui_set_lod(g_lod);
    }

    ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
    if(ImGui::BeginTabBar("layer", tab_bar_flags))
    {
      if(ImGui::BeginTabItem("favourite"))
      {
        render_darkroom_favourite();
        ImGui::EndTabItem();
      }
      if(ImGui::BeginTabItem("tweak all"))
      {
        render_darkroom_full();
        ImGui::EndTabItem();
      }
      if(ImGui::BeginTabItem("pipeline config"))
      {
        render_darkroom_pipeline();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::End();
  } // end right panel
}

} // anonymous namespace

// call from main loop:
extern "C" void dt_gui_render_frame_imgui()
{
  // Start the Dear ImGui frame
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL2_NewFrame(qvk.window);
  ImGui::NewFrame();

  switch(vkdt.view_mode)
  {
    case s_view_lighttable:
      render_lighttable();
      break;
    case s_view_darkroom:
      render_darkroom();
      break;
    default:;
  }
  ImGui::Render();
}

extern "C" void dt_gui_record_command_buffer_imgui(VkCommandBuffer cmd_buf)
{
  // Record Imgui Draw Data and draw funcs into command buffer
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_buf);
}

extern "C" void dt_gui_cleanup_imgui()
{
  widget_end(); // commit params if still ongoing
#if 0
    // Cleanup
    QVK(vkDeviceWaitIdle(g_Device));
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    CleanupVulkanWindow();
    CleanupVulkan();

    SDL_DestroyWindow(window);
    SDL_Quit();
#endif
}