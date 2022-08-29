// define this to only fit half res buffers for non-mosaiced images, too:
// #define DT_BURST_HRES_FIT // 2x2
// #define DT_BURST_QRES_FIT // 4x4
#define DT_ALIGN_NO_QUADRIC

// Uncomment the following line to disable hierarchical alignment and only use lukas kanade
// #define DT_NO_ALIGN

// Switch between LK before warp kernel (-> smoother outputs) or after warp kernel (-> more alignment noise)
#define DT_LK_BEFORE_WARP

// #define DT_ALIGN_SHIFT3
// #define DT_ALIGN_SHIFT4     // not possible on my gpu (memory)
