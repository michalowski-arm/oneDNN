/*******************************************************************************
* Copyright 2019-2025 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "gpu/intel/dispatch.h"
#include "gpu/intel/ocl_post_ops.h"
#include "gpu/intel/ocl_types.h"

#if IS_FWD == 1

KERNEL_ATTR
__kernel void ref_inner_product_fwd(__global SRC_DATA_T *src,
        __global WEI_DATA_T *wei, __global BIA_DATA_T *bias,
        __global DST_DATA_T *dst POST_OP_ARGS, __global float *src_scales,
        __global float *wei_scales, __global float *dst_scales) {

    const off_t mb = GWS_GET_MB();
    const off_t oc = GWS_GET_OC();

    if (mb >= MB || oc >= OC) return;

    ACC_DATA_T d = 0;
#if HAS_SPATIAL == 1
    for (off_t ic = 0; ic < IC; ++ic)
        for (off_t kd = 0; kd < KD; ++kd)
            for (off_t kh = 0; kh < KH; ++kh)
                for (off_t kw = 0; kw < KW; ++kw) {
                    const off_t src_off = SRC_OFF(mb, ic, kd, kh, kw);
                    const off_t wei_off = WEI_OFF(0, oc, ic, kd, kh, kw);
#else
    for (off_t ic = 0; ic < IC_TOTAL; ++ic) {
        const off_t src_off = mb * IC_TOTAL + ic;
        const off_t wei_off = oc * IC_TOTAL + ic;
#endif
                    d += SRC_TO_REF(src[src_off]) * WEI_TO_REF(wei[wei_off]);
                }
    DATA_T tmp = d;
#if WITH_SRC_SCALES
    tmp *= src_scales[0];
#endif
#if WITH_WEI_SCALES
#if WEI_SCALES_MASK == 0
    tmp *= wei_scales[0];
#else
    tmp *= wei_scales[oc];
#endif
#endif

#if WITH_BIAS
    tmp += BIA_TO_REF(bias[oc]);
#endif

    float dest_data;
#if WITH_SUM
    dest_data = DST_TO_REF(dst[mb * OC + oc]);
#endif

    APPLY_POST_OPS_SERIAL(tmp, dest_data, mb, oc, 0, 0, 0, 0);

#if WITH_DST_SCALES
    tmp /= dst_scales[0];
#endif

    dst[mb * OC + oc] = TO_DST(tmp);
}
#endif

#if IS_BWD_D == 1
KERNEL_ATTR
__kernel void ref_inner_product_bwd_data(__global SRC_DATA_T *diff_src,
        __global WEI_DATA_T *wei, __global DST_DATA_T *diff_dst) {

    const off_t mb = GWS_GET_MB_IC() / IC;
    const off_t ic = GWS_GET_MB_IC() % IC;
    const off_t kd = GWS_GET_KD();
    const off_t kh = GWS_GET_KH();
    const off_t kw = GWS_GET_KW();

    float ds = 0.0f;
    for (off_t oc = 0; oc < OC; ++oc) {
        const off_t diff_dst_off = DST_OFF(mb, oc, 0, 0, 0);
        const off_t wei_off = WEI_OFF(0, oc, ic, kd, kh, kw);
        ds += DST_TO_REF(diff_dst[diff_dst_off]) * WEI_TO_REF(wei[wei_off]);
    }
    const off_t diff_src_off = SRC_OFF(mb, ic, kd, kh, kw);
    diff_src[diff_src_off] = REF_TO_SRC(ds);
}
#endif

#if IS_BWD_W == 1
KERNEL_ATTR
__kernel void ref_inner_product_bwd_weights(__global SRC_DATA_T *src,
        __global WEI_DATA_T *diff_wei, __global BIA_DATA_T *diff_bias,
        __global DST_DATA_T *diff_dst) {

    const off_t oc = GWS_GET_OC();
    const off_t ic = GWS_GET_IC();
    const off_t kd = GWS_GET_KD();
    const off_t kh = GWS_GET_KH();
    const off_t kw = GWS_GET_KW();

    float ds = 0.0f;
    for (off_t mb = 0; mb < MB; ++mb) {
        const off_t diff_dst_off = DST_OFF(mb, oc, 0, 0, 0);
        const off_t src_off = SRC_OFF(mb, ic, kd, kh, kw);
        ds += DST_TO_REF(diff_dst[diff_dst_off]) * SRC_TO_REF(src[src_off]);
    }
    const off_t diff_wei_off = WEI_OFF(0, oc, ic, kd, kh, kw);
    diff_wei[diff_wei_off] = REF_TO_WEI(ds);
#if WITH_BIAS == 1
    if (ic == 0) {
        float db = 0.0f;
        for (off_t mb = 0; mb < MB; ++mb) {
            const off_t diff_dst_off = DST_OFF(mb, oc, 0, 0, 0);
            db += DST_TO_REF(diff_dst[diff_dst_off]);
        }
        diff_bias[oc] = REF_TO_BIA(db);
    }
#endif
}
#endif
