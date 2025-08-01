/***************************************************************************************
 * Copyright (c) 2014-2024 Zihao Yu, Nanjing University
 *
 * NEMU is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 ***************************************************************************************/

#include "mmu.h"
#include "sim.h"
#include "../../include/common.h"
#include <difftest-def.h>

#define NR_GPR MUXDEF(CONFIG_RVE, 16, 32)

static std::vector<device_factory_sargs_t> difftest_plugin_devices;
static std::vector<std::string> difftest_htif_args;
static std::vector<std::pair<reg_t, abstract_mem_t *>> difftest_mem(
    1, std::make_pair(reg_t(DRAM_BASE), new mem_t(CONFIG_MSIZE)));
static debug_module_config_t difftest_dm_config = {
    .progbufsize = 2,
    .max_sba_data_width = 0,
    .require_authentication = false,
    .abstract_rti = 0,
    .support_hasel = true,
    .support_abstract_csr_access = true,
    .support_abstract_fpr_access = true,
    .support_haltgroups = true,
    .support_impebreak = true,
};

struct diff_context_t
{
  word_t gpr[MUXDEF(CONFIG_RVE, 16, 32)];
  word_t pc;
};

class diffsim_t : public sim_t
{
public:
  processor_t *p;
  state_t *state;

  diffsim_t(const cfg_t *cfg, bool halted,
            std::vector<std::pair<reg_t, abstract_mem_t *>> mems,
            const std::vector<device_factory_sargs_t> &plugin_device_factories,
            const std::vector<std::string> &args,
            const debug_module_config_t &dm_config,
            const char *log_path,
            bool dtb_enabled, const char *dtb_file,
            bool socket_enabled,
            FILE *cmd_file, // needed for command line option --cmd
            std::optional<unsigned long long> instruction_limit)
      : sim_t(cfg, halted, mems, plugin_device_factories, args,
              dm_config, log_path, dtb_enabled, dtb_file,
              socket_enabled, cmd_file, instruction_limit)
  {
    p = NULL;
    state = NULL;
  }

  void diff_init(int port)
  {
    p = get_core(0);
    state = p->get_state();
  }

  void diff_step(uint64_t n)
  {
    // step(n);
    p->step(n);
  }

  void diff_get_regs(void *diff_context)
  {
    struct diff_context_t *ctx = (struct diff_context_t *)diff_context;
    for (int i = 0; i < NR_GPR; i++)
    {
      ctx->gpr[i] = state->XPR[i];
    }
    ctx->pc = state->pc;
  }

  void diff_set_regs(void *diff_context)
  {
    struct diff_context_t *ctx = (struct diff_context_t *)diff_context;
    for (int i = 0; i < NR_GPR; i++)
    {
      state->XPR.write(i, (sword_t)ctx->gpr[i]);
    }
    state->pc = ctx->pc;
  }

  void diff_memcpy(reg_t dest, void *src, size_t n)
  {
    mmu_t *mmu = p->get_mmu();
    for (size_t i = 0; i < n; i++)
    {
      mmu->store<uint8_t>(dest + i, *((uint8_t *)src + i));
    }
  }
};

static cfg_t cfg;
static diffsim_t *s = NULL;

extern "C"
{

  __EXPORT void difftest_memcpy(paddr_t addr, void *buf, size_t n, bool direction)
  {
    if (direction == DIFFTEST_TO_REF)
    {
      s->diff_memcpy(addr, buf, n);
    }
    else
    {
      assert(0);
    }
  }

  __EXPORT void difftest_regcpy(void *dut, bool direction)
  {
    if (direction == DIFFTEST_TO_REF)
    {
      s->diff_set_regs(dut);
    }
    else
    {
      s->diff_get_regs(dut);
    }
  }

  __EXPORT void difftest_exec(uint64_t n)
  {
    s->diff_step(n);
  }

  __EXPORT void difftest_init(int port)
  {
    difftest_htif_args.push_back("");
    const char *isa = "RV" MUXDEF(CONFIG_RV64, "64", "32") MUXDEF(CONFIG_RVE, "E", "I") "MAFDC";
    cfg.initrd_bounds = std::make_pair((reg_t)0, (reg_t)0);
    cfg.bootargs = nullptr;
    cfg.isa = isa;
    cfg.priv = DEFAULT_PRIV;
    cfg.misaligned = false;
    cfg.endianness = endianness_little;
    cfg.pmpregions = 16;
    cfg.mem_layout = std::vector<mem_cfg_t>();
    cfg.hartids = std::vector<size_t>(1);
    cfg.real_time_clint = false;
    cfg.trigger_count = 4;
    cfg.external_simulator = std::nullopt;
    if (s == NULL)
    {
      s = new diffsim_t(
          &cfg, false,
          difftest_mem, difftest_plugin_devices, difftest_htif_args,
          difftest_dm_config, nullptr, false, NULL,
          false,
          NULL,
          std::nullopt);
    }
    s->diff_init(port);
  }

  __EXPORT void difftest_raise_intr(uint64_t NO)
  {
    trap_t t(NO);
    s->p->take_trap_public(t, s->state->pc);
  }
}
