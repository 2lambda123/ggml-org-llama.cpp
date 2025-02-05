#include "pool2d.hpp"
#include <float.h>

template <typename Ti, typename To>
static void pool2d_nchw_kernel(const int ih, const int iw, const int oh, const int ow, const int kh, const int kw,
                               const int sh, const int sw, const int ph, const int pw, const int parallel_elements,
                               const Ti * src, To * dst, const enum ggml_op_pool op,
                               const sycl::nd_item<3> & item_ct1) {
    int idx = item_ct1.get_local_id(2) + item_ct1.get_group(2) * item_ct1.get_local_range(2);
    if (idx >= parallel_elements) {
        return;
    }

    const int  I_HW    = ih * iw;
    const int  O_HW    = oh * ow;
    const int  nc      = idx / O_HW;
    const int  cur_oh  = idx % O_HW / ow;
    const int  cur_ow  = idx % O_HW % ow;
    const Ti * i_ptr   = src + nc * I_HW;
    To *       o_ptr   = dst + nc * O_HW;
    const int  start_h = cur_oh * sh - ph;
    const int  bh      = sycl::max(0, start_h);
    const int  eh      = sycl::min(ih, start_h + kh);
    const int  start_w = cur_ow * sw - pw;
    const int  bw      = sycl::max(0, start_w);
    const int  ew      = sycl::min(iw, start_w + kw);

    To res = 0;

    switch (op) {
        case GGML_OP_POOL_AVG:
            res = 0;
            break;
        case GGML_OP_POOL_MAX:
            res = -FLT_MAX;
            break;
        default:
            res = (To) sycl::nan(uint32_t(0));
            break;
    }

    for (int i = bh; i < eh; i += 1) {
        for (int j = bw; j < ew; j += 1) {
#if DPCT_COMPATIBILITY_TEMP >= 350
            /*
                DPCT1098:106: The '*' expression is used instead of the __ldg
                call. These two expressions do not provide the exact same
                functionality. Check the generated code for potential precision
                and/or performance issues.
                */
            Ti cur = *(i_ptr + i * iw + j);
#else
            Ti cur = i_ptr[i * iw + j];
#endif
            switch (op) {
                case GGML_OP_POOL_AVG:
                    res += (cur / (kh * kw));
                    break;
                case GGML_OP_POOL_MAX:
                    res = sycl::max(res, (To) cur);
                    break;
                default:
                    res = (To) sycl::nan(uint32_t(0));
                    break;
            }
        }
    }
    o_ptr[cur_oh * ow + cur_ow] = res;
}

static void ggml_sycl_op_pool2d(ggml_backend_sycl_context & ctx, ggml_tensor * dst) try {
    GGML_ASSERT(dst->src[0]->type == GGML_TYPE_F32);
    GGML_ASSERT(dst->type == GGML_TYPE_F32);
    GGML_ASSERT(!ggml_backend_buffer_is_sycl_split(dst->buffer));

    const int32_t *   opts = (const int32_t *) dst->op_params;
    enum ggml_op_pool op   = static_cast<ggml_op_pool>(opts[0]);
    const int         k0   = opts[1];
    const int         k1   = opts[2];
    const int         s0   = opts[3];
    const int         s1   = opts[4];
    const int         p0   = opts[5];
    const int         p1   = opts[6];

    const int64_t IH = dst->src[0]->ne[1];
    const int64_t IW = dst->src[0]->ne[0];

    const int64_t N  = dst->ne[3];
    const int64_t OC = dst->ne[2];
    const int64_t OH = dst->ne[1];
    const int64_t OW = dst->ne[0];

    const int       parallel_elements = N * OC * OH * OW;
    const int       num_blocks        = (parallel_elements + SYCL_POOL2D_BLOCK_SIZE - 1) / SYCL_POOL2D_BLOCK_SIZE;
    dpct::queue_ptr main_stream       = ctx.stream();
    SYCL_CHECK(ggml_sycl_set_device(ctx.device));
    const float *   src0_dd           = static_cast<const float *>(dst->src[0]->data);
    float *         dst_dd            = static_cast<float *>(dst->data);
    sycl::range<3>  block_nums(1, 1, num_blocks);
    main_stream->parallel_for(sycl::nd_range<3>(block_nums * sycl::range<3>(1, 1, SYCL_IM2COL_BLOCK_SIZE),
                                                sycl::range<3>(1, 1, SYCL_IM2COL_BLOCK_SIZE)),
                              [=](sycl::nd_item<3> item_ct1) {
                                  pool2d_nchw_kernel(IH, IW, OH, OW, k1, k0, s1, s0, p1, p0, parallel_elements, src0_dd,
                                                     dst_dd, op, item_ct1);
                              });
} catch (const sycl::exception & exc) {
    std::cerr << exc.what() << "Exception caught at file:" << __FILE__ << ", line:" << __LINE__ << std::endl;
    std::exit(1);
}

void ggml_sycl_pool2d(ggml_backend_sycl_context & ctx, ggml_tensor * dst) {
    GGML_SYCL_DEBUG("call %s\n", __func__);
    ggml_sycl_op_pool2d(ctx, dst);
    GGML_SYCL_DEBUG("call %s done\n", __func__);
}
