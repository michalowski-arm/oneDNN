/*******************************************************************************
* Copyright 2017-2025 Intel Corporation
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

#include <string.h>

#include "dnnl_common.hpp"
#include "utils/parser.hpp"
#include "utils/task_executor.hpp"

#include "reorder.hpp"

namespace reorder {

TASK_EXECUTOR_DECL_TYPES;

void check_correctness(
        const settings_t &s, driver_task_executor_t &task_executor) {
    for_(const auto &i_sdt : s.sdt)
    for_(const auto &i_ddt : s.ddt)
    for_(const auto &i_stag : s.stag)
    for_(const auto &i_dtag : s.dtag)
    for_(const auto &i_strides : s.strides)
    for_(const auto &i_oflag : s.oflag)
    for_(const auto &i_cross_engine : s.cross_engine)
    for_(const auto &i_attr : s.attributes)
    for_(const auto &i_ctx_init : s.ctx_init)
    for_(const auto &i_ctx_exe : s.ctx_exe)
    for (auto i_runtime_dim_mask : s.runtime_dim_mask) {
        const prb_t prb(s.prb_dims, i_sdt, i_ddt, i_stag, i_dtag, i_strides,
                i_oflag, i_cross_engine, i_runtime_dim_mask, i_attr, i_ctx_init,
                i_ctx_exe, s.impl_filter);
        if (s.pattern && !match_regex(prb.str(), s.pattern)) return;

        task_executor.submit(prb, s.perf_template, createit, checkit, doit);
    }
}

int verify_input(const settings_t &s, const settings_t &def) {
    for_(const auto &i_scales : s.scales)
    for (auto arg : {DNNL_ARG_SRC, DNNL_ARG_DST}) {
        if (i_scales.get(arg).policy == policy_t::PER_OC) {
            BENCHDNN_PRINT(0, "%s\n",
                    "ERROR: `per_oc` policy is not supported due to "
                    "potential ambiguity. Please use one of `per_dim_0` or "
                    "`per_dim_1` policies.");
            return FAIL;
        }
    }

    for (const auto &i_cross_engine : s.cross_engine) {
        if (i_cross_engine != NONE && is_cpu()
                && bench_mode != bench_mode_t::list) {
            BENCHDNN_PRINT(0, "%s\n",
                    "ERROR: `cpu` engine does not support anything but "
                    "`--cross-engine=none`.");
            return FAIL;
        }
    }

    static constexpr int n_inputs = 2;

    for (const auto &i_strides : s.strides) {
        if (i_strides.size() != n_inputs) {
            dnnl::impl::stringstream_t ss;
            ss << vdims2str(i_strides);
            BENCHDNN_PRINT(0,
                    "Error: `strides` option expects two inputs in format "
                    "`[SRC]:[DST]` (colon must present). Current input is: "
                    "\"%s\"\n",
                    ss.str().c_str());
            SAFE_V(FAIL);
        }
        for (int i = 0; i < n_inputs; i++) {
            if (i_strides[i].size() != static_cast<size_t>(s.prb_dims.ndims)
                    && !i_strides[i].empty()) {
                dnnl::impl::stringstream_t ss;
                ss << vdims2str(i_strides);
                BENCHDNN_PRINT(0,
                        "Error: number of dimensions in the `strides` option "
                        "doesn't match the number of dimensions in the "
                        "original "
                        "problem. Current output is: \"%s\"\n",
                        ss.str().c_str());
                SAFE_V(FAIL);
            }
        }
    }

    for_(const auto &i_strides : s.strides)
    for_(const auto &i_stag : s.stag)
    for (const auto &i_dtag : s.dtag) {
        const bool strided_input
                = !i_strides[0].empty() || !i_strides[1].empty();
        if (strided_input) {
            const bool no_stride_with_stag
                    = IMPLICATION(i_stag != def.stag[0], i_strides[0].empty());
            const bool no_stride_with_dtag
                    = IMPLICATION(i_dtag != def.dtag[0], i_strides[1].empty());

            if (!no_stride_with_stag || !no_stride_with_dtag) {
                BENCHDNN_PRINT(0, "%s\n",
                        "Error: both `strides` and `tag` knobs can't be used "
                        "with either of `src`, or `dst` tensors.");
                SAFE_V(FAIL);
            }
        }
    }

    return OK;
}

static const std::string help_oflag
        = "FLAG:MASK[+...]    (Default: not specified)\n    Specifies `extra` "
          "field of destination memory descriptor.\n    `FLAG` values are "
          "`s8s8_comp` and `zp_comp`.\n    `MASK` is an non-negative integer "
          "specifying dimension to apply compensation.\n";

static const std::string help_runtime_dim_mask
        = "UINT    (Default: `0`)\n    Specifies a bit-mask that indicates "
          "whether a dimension is `DNNL_RUNTIME_DIM_VAL` if `1` on a "
          "correspondent dimension.\n";

static const std::string help_cross_engine
        = "KIND    (Default: `none`)\n    Specifies `KIND` of cross-engine "
          "used for benchmarking.\n    `KIND` values are `none`, `cpu2gpu` or "
          "`gpu2cpu`.\n";

int bench(int argc, char **argv) {
    driver_name = "reorder";
    using namespace parser;
    static settings_t s;
    static const settings_t def {};
    static driver_task_executor_t task_executor;
    for (; argc > 0; --argc, ++argv) {
        const bool parsed_options = parse_bench_settings(argv[0])
                || parse_batch(bench, argv[0])
                || parse_dt(s.sdt, def.sdt, argv[0], "sdt")
                || parse_dt(s.ddt, def.ddt, argv[0], "ddt")
                || parse_tag(s.stag, def.stag, argv[0], "stag")
                || parse_tag(s.dtag, def.dtag, argv[0], "dtag")
                || parse_strides(s.strides, def.strides, argv[0], "strides")
                || parse_multivector_option(s.oflag, def.oflag, str2flag,
                        argv[0], "oflag", help_oflag, ',', '+')
                || parse_vector_option(s.runtime_dim_mask, def.runtime_dim_mask,
                        atoi, argv[0], "runtime-dim-mask",
                        help_runtime_dim_mask)
                || parse_vector_option(s.cross_engine, def.cross_engine,
                        str2cross_engine, argv[0], "cross-engine",
                        help_cross_engine)
                || parse_driver_shared_settings(s, def, argv[0]);
        if (!parsed_options) {
            catch_unknown_options(argv[0]);

            parse_prb_dims(s.prb_dims, argv[0]);

            SAFE(verify_input(s, def), WARN);
            s.finalize();
            check_correctness(s, task_executor);
        }
    }

    task_executor.flush();

    return parse_last_argument();
}

} // namespace reorder
