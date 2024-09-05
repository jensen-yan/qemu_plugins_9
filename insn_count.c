#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    uint64_t total_insn_count;
    uint64_t czero_eqz_count;
    uint64_t czero_nez_count;
    uint64_t cmov_count;
} InsnCount;

static struct qemu_plugin_scoreboard *counts;
static qemu_plugin_u64 total_insn_count;
static qemu_plugin_u64 czero_eqz_count;
static qemu_plugin_u64 czero_nez_count;
static qemu_plugin_u64 cmov_count;      // x86 cmov* instructions,e.g. cmovne, cmovz

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        const char *insn_name = qemu_plugin_insn_disas(insn);

        if (strstr(insn_name, "czero.eqz") != NULL) {
            qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(tb, QEMU_PLUGIN_INLINE_ADD_U64, czero_eqz_count, 1);
        } else if (strstr(insn_name, "czero.nez") != NULL) {
            qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(tb, QEMU_PLUGIN_INLINE_ADD_U64, czero_nez_count, 1);                                   
        } else if (strstr(insn_name, "cmov") != NULL) { // x86 cmov* instructions
            qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(tb, QEMU_PLUGIN_INLINE_ADD_U64, cmov_count, 1);
        }

        qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(tb, QEMU_PLUGIN_INLINE_ADD_U64, total_insn_count, 1);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autofree gchar *out = g_strdup_printf("Total instructions: %" PRIu64 "\n"
                                            "czero.eqz instructions: %" PRIu64 "\n"
                                            "czero.nez instructions: %" PRIu64 "\n"
                                            "cmov instructions: %" PRIu64 "\n"
                                            "cmov / total instructions: %.2f%%\n",
                                            qemu_plugin_u64_sum(total_insn_count), 
                                            qemu_plugin_u64_sum(czero_eqz_count), 
                                            qemu_plugin_u64_sum(czero_nez_count),
                                            qemu_plugin_u64_sum(cmov_count),
                                            (double)qemu_plugin_u64_sum(cmov_count) / qemu_plugin_u64_sum(total_insn_count) * 100);
    qemu_plugin_outs(out);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    counts = qemu_plugin_scoreboard_new(sizeof(InsnCount));
    total_insn_count = qemu_plugin_scoreboard_u64_in_struct(counts, InsnCount, total_insn_count);
    czero_eqz_count = qemu_plugin_scoreboard_u64_in_struct(counts, InsnCount, czero_eqz_count);
    czero_nez_count = qemu_plugin_scoreboard_u64_in_struct(counts, InsnCount, czero_nez_count);
    cmov_count = qemu_plugin_scoreboard_u64_in_struct(counts, InsnCount, cmov_count);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}